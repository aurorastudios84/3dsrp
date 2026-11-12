package com.aurora.relay;

/** Gate expensive conversion while the codec continues decoding every input.
 * Keep cadence across source-frame quantization; retire missed deadlines. */
final class VideoFrameSelector {
 private long next;
 private boolean started;
 void reset(){started=false;}
 boolean due(long now,long interval){
  if(started&&next-now>interval)next=now+interval;
  return !started||now>=next;
 }
 void selected(long now,long interval){
  next=started?next+interval:now+interval;
  if(next<=now)next=now+interval;
  started=true;
 }
}
