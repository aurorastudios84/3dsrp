package com.aurora.relay;
import java.net.*;
/** Bounded, cancellable readiness check; a closed port is not proof of standby. */
final class ConsoleReadiness {
 static final long WAKE_TIMEOUT_MS=60_000, SETTLE_MS=5_000, POLL_MS=500;
 interface Hooks {boolean running();void status(String s);String wake();}
 interface Environment {boolean responds(String host,boolean ps5);long now();void sleep(long ms)throws InterruptedException;}
 static boolean responds(String host,boolean ps5){try(java.net.Socket s=new java.net.Socket()){s.connect(new InetSocketAddress(host,9295),700);return true;}catch(Exception e){return false;}}
 static boolean prepare(String host,boolean ps5,Hooks h)throws InterruptedException {
  return prepare(host,ps5,h,new Environment(){
   public boolean responds(String host,boolean ps5){return ConsoleReadiness.responds(host,ps5);}
   public long now(){return System.nanoTime()/1_000_000;}
   public void sleep(long ms)throws InterruptedException{Thread.sleep(ms);}
  });
 }
 static boolean prepare(String host,boolean ps5,Hooks h,Environment env)throws InterruptedException {
  h.status("Connecting to console…");if(!h.running())return false;if(env.responds(host,ps5))return h.running();
  if(!h.running())return false;h.status("Waking console… Waiting up to 60 seconds.");String error=h.wake();if(error!=null)throw new IllegalStateException(error);
  long started=env.now(),readySince=-1;
  while(h.running()){
   long remaining=WAKE_TIMEOUT_MS-(env.now()-started);if(remaining<=0)break;
   env.sleep(Math.min(POLL_MS,remaining));if(!h.running())return false;
   boolean ready=env.responds(host,ps5);long now=env.now();
   if(!h.running())return false;if(now-started>=WAKE_TIMEOUT_MS)break;
   // The listening port can appear before Remote Play finishes starting.
   // Require sustained availability after Wake; restart the grace period on a failed probe.
   if(ready){
    if(readySince<0){readySince=now;h.status("Console is waking… Waiting for Remote Play to finish starting.");}
    if(now-readySince>=SETTLE_MS){h.status("Connecting to console…");return h.running();}
   }else{
    if(readySince>=0)h.status("Waiting for console to wake…");readySince=-1;
   }
  }
  if(h.running())throw new IllegalStateException("Console did not become ready within 60 seconds. Check its address, network and Remote Play standby settings, then try again.");return false;
 }
}
