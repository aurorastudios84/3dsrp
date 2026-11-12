#!/usr/bin/env python3
"""Real C feedback encoding -> real Java controller, including loss and counter wrap."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
java = root/'android/app/src/main/java/com/aurora/relay'
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p/'feedback.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include "video_feedback.h"
int main(int argc,char **argv){
 uint8_t packet[RELAY_HEADER+RELAY_FEEDBACK_BYTES];
 RelayVideoCounters previous={.decoded=0xfffffff0,.decode_us=0xfffffff0,.shown=0xfffffff0,.pack_us=0xfffffff0,.received=0xfffffff0,.age_us=0xfffffff0,.controls=0xfffffff0};
 RelayVideoCounters now=previous;
 now.decoded+=20;now.decode_us+=800000;now.shown+=20;now.pack_us+=60000;
 now.received+=22;now.age_us+=1000000;now.controls+=60;
 relay_header(packet,RELAY_FEEDBACK,7,0,0,1,RELAY_FEEDBACK_BYTES,0,0,0xfffffff0);
 relay_video_feedback(packet+RELAY_HEADER,1000,&now,&previous,80000);
 assert(r32(packet+40)==20&&r32(packet+44)==800000&&r32(packet+76)==60);
 assert(previous.decoded==now.decoded);
 FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(packet,1,sizeof(packet),f)==sizeof(packet));fclose(f);
 relay_video_feedback(packet+RELAY_HEADER,1000,&now,&previous,0);
 for(unsigned off=8;off<48;off+=4)assert(!r32(packet+RELAY_HEADER+off));
}
''')
    subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I'+str(root/'common'),p/'feedback.c','-o',p/'feedback'],check=True)
    subprocess.run([p/'feedback',p/'feedback.bin'],check=True)
    (p/'Check.java').write_text(r'''
package com.aurora.relay;
import java.nio.*;import java.nio.file.*;
public class Check {
 static int stamp;
 static byte[] report(int decoded,int decodeUs,int shown,int packUs,int received,int missing,int ageUs){
  byte[] payload=ByteBuffer.allocate(48).putInt(1).putInt(1000).putInt(decoded).putInt(decodeUs).putInt(shown).putInt(packUs).putInt(received).putInt(missing).putInt(ageUs*shown).putInt(shown).putInt(ageUs).putInt(60).array();
  return Protocol.packet(Protocol.FEEDBACK,7,0,0,1,48,0,0,++stamp,payload,0,48);
 }
 static void check(boolean ok){if(!ok)throw new AssertionError();}
 static void feed(AdaptiveVideoRate r,long now,int decode,int pack,int missing,int age){byte[] p=report(20,20*decode,20,20*pack,20,missing,age);check(r.feedback(p,p.length,now));}
 public static void main(String[] args)throws Exception{
  AdaptiveVideoRate r=new AdaptiveVideoRate();r.reset(0);
  byte[] c=Files.readAllBytes(Paths.get(args[0]));check(r.feedback(c,c.length,1000));check(r.fps(1000)==20);
  ByteBuffer.wrap(c).putInt(28,40);check(r.feedback(c,c.length,2000)); // timestamp wraps
  check(!r.feedback(c,c.length,2500)); // duplicate doesn't refresh liveness
  ByteBuffer.wrap(c).putInt(28,39);check(!r.feedback(c,c.length,2500));
  check(r.fps(6000)==15&&r.fps(9000)==15); // no repeated fallback decay
  r.reset(0);feed(r,1000,60000,5000,0,70000);
  check(Math.abs(r.fps(1000)-880000.0/65000)<.001); // 12% headroom
  double slow=r.fps(1000);
  for(int i=0;i<2;i++)feed(r,2000+i*1000,10000,1000,0,20000);
  check(r.fps(3000)==slow);feed(r,4000,10000,1000,0,20000);check(r.fps(4000)<=slow+1.001);
  for(int i=0;i<100;i++)feed(r,5000+i*1000,10000,1000,0,20000);
  check(r.fps(104000)==30);
  double fast=r.fps(104000);feed(r,105000,10000,1000,10,20000);check(r.fps(105000)<fast);
  fast=r.fps(105000);feed(r,106000,10000,1000,0,200000);check(r.fps(106000)<fast);
  // Low/no delivery must not be interpreted as poor processing capacity.
  r.reset(0);
  for(int i=1;i<20;i++){byte[] p=report(0,0,0,0,0,0,0);check(r.feedback(p,p.length,i*1000));check(r.fps(i*1000)==20);}
  for(int i=20;i<100;i++)feed(r,i*1000,300000,10000,20,300000);
  check(r.fps(99000)==5); // bounded under prolonged pressure
  r.reset(0);byte[] renderLimited=report(20,200000,10,10000,20,0,20000);
  check(r.feedback(renderLimited,renderLimited.length,1000));check(r.fps(1000)==9);
  r.reset(0);byte[] sourceLimited=report(10,100000,10,10000,10,0,20000);
  check(r.feedback(sourceLimited,sourceLimited.length,1000));check(r.fps(1000)==20);
  r.reset(100000);check(r.fps(100000)==20);
  byte[] good=report(20,400000,20,40000,20,0,30000);
  for(int n=0;n<good.length;n++)check(!r.feedback(good,n,100001));
  check(!r.feedback(null,80,100001)&&!r.feedback(new byte[1],80,100001));
  for(int offset:new int[]{16,18,20,32,36,40,44,48,52,56,60,68,76}){
   byte[] bad=good.clone();ByteBuffer.wrap(bad).putInt(offset,-1);check(!r.feedback(bad,bad.length,100001));
  }
  check(r.feedback(good,good.length,100001));check(r.intervalMs(100001)>=34);
 }
}
''')
    subprocess.run(['javac','-d',tmp,java/'Protocol.java',java/'AdaptiveVideoRate.java',p/'Check.java'],check=True)
    subprocess.run(['java','-cp',tmp,'com.aurora.relay.Check',p/'feedback.bin'],check=True)
    # Exercise the production diagnostic sender with oversized lifetime counters.
    source=(java/'RelayEngine.java').read_text()
    ages=source[source.index(' String phoneAges()'):source.index(' String diagnostics()')]
    sender=source[source.index(' String compactDiagnostics()'):source.index(' String stats()')]
    (p/'DiagCheck.java').write_text(r'''
package com.aurora.relay;
import java.util.concurrent.atomic.*;import java.io.IOException;
class BuildConfig {static final int VERSION_CODE=26;}
class SystemClock {static long elapsedRealtime(){return 1000;}}
public class DiagCheck {
 int width=400,height=225;double inputFps=Double.MAX_VALUE,decodedFps=60,sentFps=15;
 AtomicLong queueAgeUs=new AtomicLong(Long.MAX_VALUE),decodeAgeUs=queueAgeUs,sendAgeUs=queueAgeUs,maxPhoneAgeUs=queueAgeUs,videoDrops=queueAgeUs,audioDrops=queueAgeUs;
 java.util.Queue<Integer> video=new java.util.ArrayDeque<>();AdaptiveVideoRate videoRate=new AdaptiveVideoRate();
 String diagnostics(){return new String(new char[5000]).replace('\0','x');}
 void send(int type,int seq,int i,int count,int total,int w,int h,int stamp,byte[] b,int off,int length){
  if(type!=8||length>Protocol.CHUNK)throw new AssertionError();System.out.println(new String(b,java.nio.charset.StandardCharsets.UTF_8));
 }
 METHODS
 public static void main(String[] args)throws Exception{new DiagCheck().sendDiagnostics();}
}
'''.replace('METHODS',ages+sender))
    subprocess.run(['javac','-cp',tmp,'-d',tmp,p/'DiagCheck.java'],check=True)
    import json
    diagnostic=json.loads(subprocess.check_output(['java','-cp',tmp,'com.aurora.relay.DiagCheck'],text=True))
    assert diagnostic['compact'] and len(diagnostic['phone_age_us'])==5 and len(diagnostic['adaptive'])==5
print('PASS C/Java feedback: headroom, bounded rates, slow recovery, loss/age pressure, starvation, expiry, restart, malformed/reordered packets and counter/timestamp wrap')
