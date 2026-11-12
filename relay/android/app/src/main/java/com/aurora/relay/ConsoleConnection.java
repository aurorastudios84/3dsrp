package com.aurora.relay;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/** Owns native startup attempts. Cleanup always runs on this worker, never a native callback. */
final class ConsoleConnection {
 static final int MAX_ATTEMPTS=3;
 static final long STARTUP_MS=60_000, RETRY_MS=5_000;
 interface Hooks {
  boolean running();String start(int attempt);void cleanup();void status(String text);void failed(String text);
 }
 static final class Event {
  final boolean connected,retryable;final String message;
  Event(boolean connected,boolean retryable,String message){this.connected=connected;this.retryable=retryable;this.message=message;}
 }
 interface Environment {long now();Event await(long ms)throws InterruptedException;}
 private final BlockingQueue<Event> events=new LinkedBlockingQueue<>();
 void connected(){events.offer(new Event(true,false,"Console connected."));}
 void quit(String message,boolean retryable){events.offer(new Event(false,retryable,message));}
 void run(Hooks hooks)throws InterruptedException {
  run(hooks,new Environment(){
   public long now(){return System.nanoTime()/1_000_000;}
   public Event await(long ms)throws InterruptedException{return events.poll(ms,TimeUnit.MILLISECONDS);}
  });
 }
 void run(Hooks h,Environment env)throws InterruptedException {
  for(int attempt=1;attempt<=MAX_ATTEMPTS&&h.running();attempt++){
   long deadline=env.now()+STARTUP_MS;
   events.clear();
   h.status(attempt==1?"Connecting to console…":"Connecting to console… Attempt "+attempt+" of "+MAX_ATTEMPTS);
   String error=h.start(attempt);
   if(!h.running())return;
   if(error!=null){h.failed(error);return;}
   boolean established=false;
   Event ended=null;
   while(h.running()){
    long remaining=deadline-env.now();
    if(!established&&remaining<=0){ended=new Event(false,true,"Console connection timed out after 60 seconds. Check Remote Play on the console and try again.");break;}
    Event event=env.await(established?250:Math.min(250,remaining));
    if(!h.running())return;
    if(event==null)continue;
    if(event.connected){established=true;h.status(event.message);continue;}
    ended=event;break;
   }
   if(!h.running()||ended==null)return;
   if(established||attempt==MAX_ATTEMPTS){h.failed(ended.message);return;}
   h.cleanup(); // Join and finalize the old native session before obtaining a fresh handshake.
   if(!h.running())return;
   // Retry each startup rejection under the requested three-attempt policy.
   long delay=RETRY_MS;
   h.status("Console not ready. Retrying in "+delay/1000+" seconds… ("+attempt+"/"+MAX_ATTEMPTS+")");
   long resume=env.now()+delay;
   // Ignore shutdown events from the old attempt and keep Stop responsive.
   while(h.running()&&env.now()<resume)env.await(Math.min(250,resume-env.now()));
  }
 }
}
