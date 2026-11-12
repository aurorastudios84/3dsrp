package com.aurora.relay;

/** Allocation-free event timing. Each probe has one writer; snapshots are approximate.
 * Values: events, longest gap us, gaps >=75ms, gaps >=150ms, longest work us,
 *         gaps >=300ms, longest consecutive run with gaps >=75ms.
 * First event has no gap. No file IO, picture copies, or per-event locking.
 */
final class FrameTiming {
 private long last,run;
 private volatile long events,maxGap,over75,over150,maxWork,over300,maxRun;
 void event(long now) {
  if(events!=0){long gap=Math.max(0,now-last);maxGap=Math.max(maxGap,gap);
   if(gap>=75000){over75++;run++;maxRun=Math.max(maxRun,run);}else run=0;
   if(gap>=150000)over150++;if(gap>=300000)over300++;
  }
  last=now;events++;
 }
 void work(long elapsed){maxWork=Math.max(maxWork,elapsed);}
 String snapshot(){return "["+events+","+maxGap+","+over75+","+over150+","+maxWork+","+over300+","+maxRun+"]";}
}
