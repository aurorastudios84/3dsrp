package com.aurora.relay;

/** Bounded 20ms PCM packets. Never accumulate an unbounded sound delay. */
final class PcmPacketQueue {
 static final int BYTES=480,PERIOD_US=20000;
 private final byte[][] slots=new byte[3][BYTES];
 private final byte[] partial=new byte[BYTES];
 private int head,count,used,startup=2;private long next;
 private long dropped,maxQueued;
 synchronized void offer(byte[] data){
  for(int off=0;off<data.length;){int n=Math.min(BYTES-used,data.length-off);System.arraycopy(data,off,partial,used,n);used+=n;off+=n;
   if(used==BYTES){if(count==slots.length){head=(head+1)%slots.length;count--;dropped++;}System.arraycopy(partial,0,slots[(head+count)%slots.length],0,BYTES);count++;maxQueued=Math.max(maxQueued,count);used=0;}
  }
 }
 synchronized boolean poll(long now,byte[] out){
  if(count==0||now<next||(startup==2&&count<2))return false;
  System.arraycopy(slots[head],0,out,0,BYTES);head=(head+1)%slots.length;count--;
  // Prime the receiver with two packets (40ms), then keep a regular clock.
  // Late scheduling never emits back-to-back catch-up packets after startup.
  if(startup>0){startup--;next=now+(startup>0?0:PERIOD_US);}
  else next=Math.max(next+PERIOD_US,now+PERIOD_US/2);
  return true;
 }
 synchronized long waitUs(long now){return count==0||(startup==2&&count<2)?1000:Math.max(0,Math.min(PERIOD_US,next-now));}
 synchronized void clear(){head=count=used=0;next=0;startup=2;}
 synchronized String snapshot(){return "["+dropped+","+maxQueued+","+count+"]";}
}
