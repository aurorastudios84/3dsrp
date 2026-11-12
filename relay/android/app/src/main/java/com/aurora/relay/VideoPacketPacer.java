package com.aurora.relay;

import java.util.concurrent.locks.LockSupport;
import java.util.function.BooleanSupplier;
import java.util.function.LongConsumer;
import java.util.function.LongSupplier;

/** Small UDP pictures stay quick; larger pictures spread across about 12ms.
 * Schedule from the actual previous send, never catch up with a packet burst. */
final class VideoPacketPacer {
 static final long WINDOW_NS=12_000_000L, MAX_GAP_NS=1_000_000L;
 private final LongSupplier clock;
 private final LongConsumer park;
 private final long gap;
 private long next;
 private boolean started;
 VideoPacketPacer(int fragments){this(fragments,System::nanoTime,LockSupport::parkNanos);}
 VideoPacketPacer(int fragments,LongSupplier clock,LongConsumer park){
  if(fragments<1)throw new IllegalArgumentException("fragment count");
  gap=fragments==1?0:Math.min(MAX_GAP_NS,WINDOW_NS/(fragments-1));
  this.clock=clock;this.park=park;
 }
 boolean await(BooleanSupplier running){
  while(running.getAsBoolean()){
   long remaining=started?next-clock.getAsLong():0;
   if(remaining<=0)return true;
   park.accept(remaining);
  }
  return false;
 }
 void sent(){next=clock.getAsLong()+gap;started=true;}
}
