#!/usr/bin/env python3
"""Reproduce production startup timing: HELLO and usage must retain one launch ID."""
from pathlib import Path
import subprocess,tempfile,re,sys
root=Path(__file__).resolve().parents[1];app=root/'3ds/shared';java=root/'android/app/src/main/java/com/aurora/relay'
source=(root/'3ds/main.c').read_text()
reproduce_old='--reproduce-old' in sys.argv
if reproduce_old:
 source=source.replace('const uint32_t run_id=(uint32_t)started;','').replace('usage_relay_init(run_id,send_message)','usage_relay_init((uint32_t)started,send_message)').replace('send_message(RELAY_HELLO,NULL,0,run_id)','send_message(RELAY_HELLO,NULL,0,(uint32_t)started)')
main=source[source.index('int main(void)'):]
sender=source[source.index('static void send_message('):source.index('static void receive_worker(')]
# Use the actual startup expressions, timer reset and HELLO call. A hand-written
# matching ID test missed the real bug: startup changes the duration timer.
initial=re.search(r'u64 started=osGetTime\(\);(?:const uint32_t run_id=\(uint32_t\)started;)?',main)[0]
usage_init=re.search(r'usage_relay_init\([^;]+;',main)[0]
reset=re.search(r'\n (started=osGetTime\(\);)',main)[1]
hello=re.search(r'send_message\(RELAY_HELLO,NULL,0,[^;]+;',main)[0]
c=r'''
#include "usage_relay.h"
#include "usage_platform.h"
#include "wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
typedef unsigned long long u64;
static atomic_uint current_session=0x91234567u,send_errors;static atomic_int send_errno;
static int sock=-1;static struct sockaddr_in phone;static unsigned calls;
static u64 osGetTime(void){return calls++?0x180001234ULL:0x180000000ULL;}
int64_t usage_platform_epoch(void){return 1793923200;}
bool usage_random_uuid(char uuid[37]){static uint8_t n;uint8_t bytes[16]={0};bytes[15]=++n;usage_uuid_from_random(uuid,bytes);return true;}
static ssize_t capture(int socket,const void *data,size_t size,int flags,const struct sockaddr *to,socklen_t length){
 const uint8_t *p=data;const char *path=p[4]==RELAY_HELLO?"hello.bin":"report.bin";
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,size,f)==size);fclose(f);return size;
}
#define sendto capture
'''+sender+r'''
int main(void){
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/3dsrp-relay",0700);
'''+initial+'\n'+usage_init+r'''
 assert(usage_relay_choose(true));usage_relay_frame(7,1000);usage_relay_frame(7,2000);usage_relay_suspend();
'''+reset+'\n'+hello+r'''
 usage_relay_tick(true,false,3000);assert(!send_errors);
 assert(started==0x180001234ULL);return 0;
}
'''
j=r'''
package com.aurora.relay;
import java.nio.*;import java.nio.file.*;import java.util.concurrent.*;
public class StartupTest {
 public static void main(String[] args)throws Exception{
  byte[] hello=Files.readAllBytes(Path.of(args[0],"hello.bin")),report=Files.readAllBytes(Path.of(args[0],"report.bin"));
  int handshakeRun=ByteBuffer.wrap(hello).getInt(28),payloadRun=ByteBuffer.wrap(report).getInt(32);
  if(handshakeRun!=payloadRun)throw new AssertionError("Actual startup reset changed HELLO run ID; Android rejects report");
  if(handshakeRun!=(int)0x80000000)throw new AssertionError("Launch ID was reset");
  CountDownLatch accepted=new CountDownLatch(1);
  try(UsageProxy proxy=new UsageProxy(json->204,(run,serial,outcome)->{if(run==handshakeRun&&outcome==1)accepted.countDown();})){
   proxy.reset(handshakeRun);proxy.report(report,report.length);if(!accepted.await(2,TimeUnit.SECONDS))throw new AssertionError("Production proxy rejected startup report");
  }
  System.out.println("PASS actual startup clock reset: immutable HELLO/report run ID, signed32-bit value, production Android acceptance");
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(c);(p/'StartupTest.java').write_text(j)
 (p/'3ds.h').write_text('typedef int LightLock;static void LightLock_Lock(LightLock *p){}static void LightLock_Unlock(LightLock *p){}')
 files=[root/'3ds/usage_relay.c']+[app/'source'/f'{name}.c' for name in ['usage_stats','usage_preferences','usage_journal','pair_store']]
 subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(p),'-I'+str(root/'3ds'),'-I'+str(root/'common'),'-I'+str(app/'include'),str(p/'test.c'),*map(str,files),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)
 subprocess.run(['javac','-d',tmp,*[str(java/(name+'.java')) for name in ['Protocol','UsageDelivery','UsageProxy']],str(p/'StartupTest.java')],check=True)
 result=subprocess.run(['java','-cp',tmp,'com.aurora.relay.StartupTest',tmp],text=True,capture_output=True)
 if reproduce_old:
  assert result.returncode!=0 and 'Actual startup reset changed HELLO run ID' in result.stderr,result.stderr
  print('PASS reproduced release41 failure: startup reset makes Android reject the run ID')
 else:
  assert result.returncode==0,result.stderr
  print(result.stdout.strip())
