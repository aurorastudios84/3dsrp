#!/usr/bin/env python3
"""Exercise the production UDP/HTTPS proxy and handheld accounting/SD recovery."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
app=root/'3ds/shared'
java=r'''
package com.aurora.relay;
import java.nio.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
public final class UsageTest {
 static final String ID="01234567-89ab-4def-8123-456789abcdef";
 static byte[] packet(int run,int serial){
  byte[] data=new byte[96];ByteBuffer b=ByteBuffer.wrap(data);b.putInt(run);b.put(ID.getBytes());b.put(ID.getBytes());b.put("2026-11-05".getBytes());b.put((byte)1).put((byte)0).putInt(86400).putInt(7);
  return Protocol.packet(Protocol.USAGE_REPORT,99,0,0,1,96,0,0,serial,data,0,96);
 }
 static void check(boolean ok){if(!ok)throw new AssertionError();}
 public static void main(String[] args)throws Exception {
  byte[] p=packet(1,1);String body=UsageProxy.body(p,p.length,1);
  check(body.contains("\"app\":\"original_3ds\"")&&body.contains("\"seconds\":86400")&&body.length()<=192);
  check(UsageProxy.body(p,p.length-1,1)==null&&UsageProxy.body(p,p.length,2)==null);
  for(int offset:new int[]{16,18,20,32,36,72,108,118,119,120,124}){
   byte[] bad=p.clone();bad[offset]=(byte)255;check(UsageProxy.body(bad,bad.length,1)==null);
  }
  for(int code:new int[]{204,400,410,403,500,0}){
   CountDownLatch ack=new CountDownLatch(1);AtomicInteger result=new AtomicInteger();
   try(UsageProxy proxy=new UsageProxy(json->{if(code==0)throw new java.io.IOException();return code;},(run,serial,value)->{check(run==1&&serial==1);result.set(value);ack.countDown();})){
    proxy.reset(1);proxy.report(p,p.length);check(ack.await(2,TimeUnit.SECONDS));check(result.get()==(code==204?1:code==410?2:-1));
   }
  }
  CountDownLatch started=new CountDownLatch(1),release=new CountDownLatch(1);AtomicInteger calls=new AtomicInteger(),acks=new AtomicInteger();
  try(UsageProxy proxy=new UsageProxy(json->{calls.incrementAndGet();started.countDown();release.await();return 204;},(run,serial,value)->acks.incrementAndGet())){
   proxy.reset(1);proxy.report(p,p.length);check(started.await(2,TimeUnit.SECONDS));proxy.report(p,p.length); // duplicate cannot submit twice
   for(int i=2;i<30;i++){byte[] next=packet(1,i);proxy.report(next,next.length);} // bounded queue rejects without blocking
   check(calls.get()==1&&acks.get()>0);
   int before=acks.get();proxy.reset(2);release.countDown();Thread.sleep(100);check(acks.get()==before&&calls.get()==1); // cancelled active/queued jobs cannot ACK a new handheld
  }
  CountDownLatch cancelStarted=new CountDownLatch(1),cancelRelease=new CountDownLatch(1);AtomicInteger cancelledAcks=new AtomicInteger();
  try(UsageProxy proxy=new UsageProxy(json->{cancelStarted.countDown();cancelRelease.await();return 204;},(r,s,v)->cancelledAcks.incrementAndGet())){
   proxy.reset(1);proxy.report(p,p.length);check(cancelStarted.await(2,TimeUnit.SECONDS));
   byte[] cancel=Protocol.packet(Protocol.USAGE_CANCEL,99,0,0,1,4,0,0,1,ByteBuffer.allocate(4).putInt(1).array(),0,4);
   proxy.cancel(cancel,cancel.length);cancelRelease.countDown();Thread.sleep(100);check(cancelledAcks.get()==0);
  }
  System.out.println("PASS bounded HTTPS proxy: validation, fixed app, outcomes, duplicate, cancellation and peer reset");
 }
}
'''
c=r'''
#include "usage_relay.h"
#include "usage_platform.h"
#include "wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static uint8_t sent[128];static unsigned length,type,reports;static uint32_t serial;
int64_t usage_platform_epoch(void){return 1793836800;}
bool usage_random_uuid(char uuid[37]){static unsigned n=0;uint8_t bytes[16]={0};bytes[15]=++n;usage_uuid_from_random(uuid,bytes);return true;}
static void send_packet(unsigned t,const uint8_t *p,unsigned n,uint32_t stamp){type=t;length=n;serial=stamp;if(n)memcpy(sent,p,n);if(t==RELAY_USAGE_REPORT)reports++;}
static void ack(uint32_t run,uint32_t seq,int outcome){uint8_t p[40];relay_header(p,RELAY_USAGE_ACK,99,0,0,1,8,0,0,seq);w32(p+32,run);w32(p+36,outcome);usage_relay_receive(p,sizeof(p));}
int main(void){
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/3dsrp-relay",0700);
 usage_relay_init(7,send_packet);assert(usage_relay_unasked()&&!usage_relay_allowed());
 usage_relay_frame(99,1000);usage_relay_frame(99,3000);usage_relay_shutdown(true);assert(!reports);
 assert(usage_relay_choose(false));usage_relay_shutdown(true);usage_relay_init(7,send_packet);assert(!usage_relay_unasked()&&!usage_relay_allowed());
 assert(usage_relay_choose(true));assert(usage_relay_allowed());
 usage_relay_frame(99,4000);usage_relay_frame(99,7000);usage_relay_suspend();
 usage_relay_tick(true,false,15000);assert(type==RELAY_USAGE_REPORT&&length==96&&r32(sent)==7&&r32(sent+88)==3&&sent[86]==1);
 uint8_t previous[96];memcpy(previous,sent,96);uint32_t pending=serial;
 ack(8,pending,1);usage_relay_tick(true,false,15001); // wrong client run cannot delete
 assert(access("sdmc:/3ds/3dsrp-relay/usage-pending.bin",F_OK)==0);
 usage_relay_shutdown(false);usage_relay_init(8,send_packet);usage_relay_tick(true,false,16000);
 assert(type==RELAY_USAGE_REPORT&&r32(sent)==8&&r32(sent+88)==3&&!memcmp(previous+4,sent+4,82)); // same install/session/day after lost ACK/restart
 ack(8,serial,1);usage_relay_tick(true,false,16001);unsigned accepted=reports;
 usage_relay_tick(true,false,16002);assert(reports==accepted);
 usage_relay_frame(100,20000);usage_relay_frame(100,21000);usage_relay_suspend();
 usage_relay_frame(100,30000);usage_relay_frame(100,32000);usage_relay_tick(true,false,35000);assert(r32(sent+88)==3); // HOME/sleep gap excluded
 // Failed persistence on withdrawal still cancels and deletes pending activity immediately.
 assert(rename("sdmc:/3ds/3dsrp-relay/usage.bin","sdmc:/3ds/3dsrp-relay/usage.saved")==0);
 mkdir("sdmc:/3ds/3dsrp-relay/usage.bin",0700);
 assert(!usage_relay_choose(false)&&!usage_relay_allowed());
 assert(access("sdmc:/3ds/3dsrp-relay/usage-pending.bin",F_OK)!=0);
 rmdir("sdmc:/3ds/3dsrp-relay/usage.bin");assert(rename("sdmc:/3ds/3dsrp-relay/usage.saved","sdmc:/3ds/3dsrp-relay/usage.bin")==0);
 unsigned withdrawn=reports;assert(usage_relay_choose(true));usage_relay_tick(true,false,35500);assert(reports==withdrawn);
 assert(usage_relay_choose(false));usage_relay_shutdown(true);assert(!usage_relay_allowed());
 usage_relay_init(9,send_packet);assert(!usage_relay_allowed());unsigned declined=reports;usage_relay_tick(true,false,36000);assert(reports==declined);
 // Storage failure must disable logging, never silently retain permission.
 unlink("sdmc:/3ds/3dsrp-relay/usage.bin");mkdir("sdmc:/3ds/3dsrp-relay/usage.bin",0700);
 assert(!usage_relay_choose(true)&&!usage_relay_allowed());
 puts("PASS handheld consent, real-frame duration, suspend gaps, lost ACK/relaunch recovery, ACK validation and fail-closed SD failure");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp)
 (tmp/'UsageTest.java').write_text(java)
 src=root/'android/app/src/main/java/com/aurora/relay'
 subprocess.run(['javac','-d',str(tmp),str(src/'Protocol.java'),str(src/'UsageProxy.java'),str(src/'UsageDelivery.java'),str(tmp/'UsageTest.java')],check=True)
 subprocess.run(['java','-cp',str(tmp),'com.aurora.relay.UsageTest'],check=True)
 (tmp/'3ds.h').write_text('typedef int LightLock;\nstatic void LightLock_Lock(LightLock *p){}\nstatic void LightLock_Unlock(LightLock *p){}\n')
 (tmp/'test.c').write_text(c)
 files=[root/'3ds/usage_relay.c']+[app/'source'/f'{name}.c' for name in ['usage_stats','usage_preferences','usage_journal','pair_store']]
 subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(tmp),'-I'+str(root/'3ds'),'-I'+str(root/'common'),'-I'+str(app/'include'),str(tmp/'test.c'),*map(str,files),'-o',str(tmp/'test')],check=True)
 subprocess.run([str(tmp/'test')],cwd=tmp,check=True)
