package com.aurora.relay;

import java.nio.ByteBuffer;

/** Uses receiver service time, not incoming/displayed FPS, to avoid starvation feedback. */
final class AdaptiveVideoRate {
 static final int FEEDBACK_BYTES=48;
 private double fps=20,serviceUs;
 private long resetAt,lastFeedback;
 private int lastStamp,healthy;
 private boolean seen;
 private double localAgeUs,displayFps;

 synchronized void reset(long now){fps=20;serviceUs=0;resetAt=now;lastFeedback=0;seen=false;healthy=0;localAgeUs=displayFps=0;}
 synchronized boolean feedback(byte[] packet,int length,long now){
  if(packet==null||length!=Protocol.HEADER+FEEDBACK_BYTES||packet.length<length||!Protocol.valid(packet,length))return false;
  ByteBuffer b=ByteBuffer.wrap(packet);
  if((packet[4]&255)!=Protocol.FEEDBACK||b.getInt(20)!=FEEDBACK_BYTES||b.getShort(16)!=0||b.getShort(18)!=1||b.getInt(32)!=1)return false;
  int stamp=b.getInt(28);
  if(seen&&stamp-lastStamp<=0)return false;
  long[] v=new long[12];for(int i=0;i<v.length;i++)v[i]=b.getInt(32+4*i)&0xffffffffL;
  long ms=v[1],decoded=v[2],shown=v[4],received=v[6],missing=v[7];
  if(ms<500||ms>5000||decoded>600||shown>600||received>1000||missing>10000||v[3]>10000000||v[5]>10000000||v[9]!=shown||v[11]>1000)return false;
  lastStamp=stamp;lastFeedback=now;seen=true;
  displayFps=shown*1000.0/ms;localAgeUs=shown==0?0:v[8]/(double)shown;
  // An input/network pause supplies no evidence that the decoder is slower.
  if(decoded<3||shown<3||v[3]==0){healthy=0;return true;}
  double sample=v[3]/(double)decoded+v[5]/(double)shown;
  serviceUs=serviceUs==0?sample:serviceUs*.7+sample*.3;
  double capacity=Math.max(5,Math.min(30,880000/serviceUs));
  // A display bottleneck is evidence only when received AND decoded supply
  // exceeded presentation. Low source FPS alone must never lower the target.
  if(received>=8&&decoded>=8&&shown<Math.min(received,decoded)*.85)
   capacity=Math.max(5,Math.min(capacity,displayFps*.9));
  boolean pressured=(received+missing>=5&&missing/(double)(received+missing)>.10)||localAgeUs>150000;
  double target=pressured?Math.min(capacity,fps*.85):capacity;
  if(target<fps*.95){fps=Math.max(5,target);healthy=0;}
  else if(!pressured&&++healthy>=3){fps=Math.min(capacity,Math.min(30,fps+1));healthy=0;}
  else if(pressured)healthy=0;
  return true;
 }
 synchronized double fps(long now){
  // Older clients and lost feedback use a bounded conservative fallback.
  if(now-(seen?lastFeedback:resetAt)>3500){fps=Math.min(fps,15);healthy=0;}
  return fps;
 }
 synchronized long intervalMs(long now){return (long)Math.ceil(1000/fps(now));}
 synchronized long intervalNs(long now){return (long)Math.ceil(1000000000/fps(now));}
 synchronized String snapshot(long now){return "["+Math.round(fps(now)*100)+","+Math.round(serviceUs)+","+Math.round(displayFps*100)+","+Math.round(localAgeUs)+","+(seen?Math.max(0,now-lastFeedback):-1)+"]";}
}
