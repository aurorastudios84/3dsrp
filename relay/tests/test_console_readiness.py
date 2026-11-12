#!/usr/bin/env python3
"""Exercise real socket probes and deterministic wake timing/cancellation."""
from pathlib import Path
import tempfile, subprocess
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);src=root/'android/app/src/main/java/com/aurora/relay/ConsoleReadiness.java'
 (p/'Check.java').write_text('''package com.aurora.relay;
import java.net.*;
class Check {
 static class Fake implements ConsoleReadiness.Environment {
  long time,readyAt=30_000,flapAt=-1,flapUntil=-1,cancelAt=-1;int probes;H hooks;
  public boolean responds(String host,boolean ps5){probes++;return time>=readyAt&&!(time>=flapAt&&time<flapUntil);}
  public long now(){return time;}
  public void sleep(long ms){assert ms>0&&ms<=500;time+=ms;if(cancelAt>=0&&time>=cancelAt)hooks.live=false;}
 }
 static void timing()throws Exception {
  H h=new H();Fake f=new Fake();
  assert ConsoleReadiness.prepare("fake",true,h,f)&&h.wakes==1&&f.time==35_000; // Beyond the old 20s limit.
  h=new H();f=new Fake();f.readyAt=500;
  assert ConsoleReadiness.prepare("fake",false,h,f)&&f.time==5_500; // No immediate connect on first open port.
  h=new H();f=new Fake();f.readyAt=500;f.flapAt=3_000;f.flapUntil=4_000;
  assert ConsoleReadiness.prepare("fake",true,h,f)&&f.time==9_000; // Readiness must settle again after a flap.
  h=new H();f=new Fake();f.readyAt=Long.MAX_VALUE;
  try{ConsoleReadiness.prepare("fake",true,h,f);throw new AssertionError();}catch(IllegalStateException e){assert e.getMessage().contains("60 seconds")&&f.time==60_000&&h.wakes==1;}
  h=new H();f=new Fake();f.readyAt=58_000;
  try{ConsoleReadiness.prepare("fake",true,h,f);throw new AssertionError();}catch(IllegalStateException e){assert f.time==60_000;} // Settling cannot extend deadline.
  for(long cancelAt:new long[]{1_000,32_000}){h=new H();f=new Fake();f.cancelAt=cancelAt;f.hooks=h;
   assert !ConsoleReadiness.prepare("fake",true,h,f)&&f.time==cancelAt&&h.wakes==1;}
  h=new H();f=new Fake();f.readyAt=0;
  assert ConsoleReadiness.prepare("fake",true,h,f)&&h.wakes==0&&f.time==0;
  h=new H();h.error="wake failed";f=new Fake();
  try{ConsoleReadiness.prepare("fake",true,h,f);throw new AssertionError();}catch(IllegalStateException e){assert e.getMessage().equals("wake failed")&&f.time==0;}
 }
 static class H implements ConsoleReadiness.Hooks { boolean live=true;int wakes;String error;public boolean running(){return live;}public void status(String s){}public String wake(){wakes++;return error;} }
 public static void main(String[] a)throws Exception {
  timing();
  H h=new H();h.live=false;assert !ConsoleReadiness.prepare("127.0.0.1",true,h)&&h.wakes==0;
  try(ServerSocket server=new ServerSocket(9295,2,InetAddress.getByName("127.0.0.1"))){h=new H();assert ConsoleReadiness.prepare("127.0.0.1",true,h)&&h.wakes==0;}
  h=new H();h.error="wake failed";try{ConsoleReadiness.prepare("127.0.0.1",false,h);throw new AssertionError();}catch(IllegalStateException e){assert e.getMessage().equals("wake failed")&&h.wakes==1;}
  H cancel=new H(){public String wake(){wakes++;live=false;return null;}};assert !ConsoleReadiness.prepare("127.0.0.1",true,cancel)&&cancel.wakes==1;
  H asleep=new H(){public String wake(){wakes++;new Thread(()->{try{Thread.sleep(100);try(ServerSocket server=new ServerSocket(9295)){server.setSoTimeout(10000);for(int i=0;i<11;i++)server.accept().close();}}catch(Exception e){throw new RuntimeException(e);}}).start();return null;}};
  assert ConsoleReadiness.prepare("127.0.0.1",true,asleep)&&asleep.wakes==1;
 }
}''')
 subprocess.run(['javac','-d',tmp,str(src),str(p/'Check.java')],check=True)
 subprocess.run(['java','-ea','-cp',tmp,'com.aurora.relay.Check'],check=True,timeout=25)
print('PASS real TCP probes; 30s wake; 5s settling; flapping readiness; 60s deadline; cancellation during wake/settling; ready fast path; wake errors')
