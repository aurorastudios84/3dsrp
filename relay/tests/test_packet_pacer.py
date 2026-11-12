#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp)
 (p/'Check.java').write_text('''package com.aurora.relay;
public class Check {
 static long now;static int parks;static boolean running;
 static void check(boolean value){if(!value)throw new AssertionError();}
 public static void main(String[] args){
  for(int count=1;count<=86;count++){
   now=0;parks=0;running=true;
   VideoPacketPacer p=new VideoPacketPacer(count,()->now,n->{check(n>0);now+=n;parks++;});
   long gap=count==1?0:Math.min(1_000_000L,12_000_000L/(count-1));
   long previous=0;
   for(int i=0;i<count;i++){
    check(p.await(()->running));check(now==i*gap);if(i>0)check(now-previous==gap);
    previous=now;p.sent();
   }
   check(now<=12_000_000L);check(parks==count-1);
  }
  // A delayed sender must not catch up by firing the remaining packets at once.
  now=0;running=true;
  VideoPacketPacer p=new VideoPacketPacer(15,()->now,n->now+=n);
  check(p.await(()->running));p.sent();now+=20_000_000L;
  check(p.await(()->running));long last=now;p.sent();check(p.await(()->running));check(now>last);
  // Spurious wakeups cannot let packets out early; shutdown cancels a wait.
  now=0;parks=0;
  p=new VideoPacketPacer(2,()->now,n->{now+=Math.max(1,n/2);parks++;});p.sent();check(p.await(()->running));check(now==1_000_000L&&parks>1);
  now=0;running=true;
  p=new VideoPacketPacer(2,()->now,n->running=false);p.sent();check(!p.await(()->running));
  System.out.println("PASS pacing for 1–86 fragments, bounded frame spread, no catch-up bursts, spurious wakes and cancellation");
 }
}''')
 subprocess.run(['javac','-d',str(p),str(root/'android/app/src/main/java/com/aurora/relay/VideoPacketPacer.java'),str(p/'Check.java')],check=True)
 subprocess.run(['java','-cp',str(p),'com.aurora.relay.Check'],check=True)
