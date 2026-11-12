#!/usr/bin/env python3
"""Production output draining/conversion gate with a fake codec and deterministic clock."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
java=root/'android/app/src/main/java/com/aurora/relay'
source=(java/'RelayEngine.java').read_text()
methods=source[source.index(' void observeVideoOutput('):source.index(' void scaleFast(')]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);media=p/'android/media';media.mkdir(parents=True)
 (media/'Image.java').write_text('''package android.media;
public class Image {public final int id;public boolean closed;public Image(int id){this.id=id;}public void close(){if(closed)throw new AssertionError();closed=true;}}
''')
 (media/'MediaCodec.java').write_text('''package android.media;
import java.util.*;
public class MediaCodec {
 public static final int BUFFER_FLAG_CODEC_CONFIG=2;
 public static class BufferInfo {public int offset,size,flags;public long presentationTimeUs;public void set(int o,int s,long p,int f){offset=o;size=s;presentationTimeUs=p;flags=f;}}
 public final ArrayDeque<Integer> queued=new ArrayDeque<>();public final Set<Integer> owned=new HashSet<>(),released=new HashSet<>();
 public final Map<Integer,Integer> flags=new HashMap<>();public int imageCalls;public Image lastImage;public boolean nullImage;
 public void add(int id){queued.add(id);}
 public int dequeueOutputBuffer(BufferInfo info,long timeout){if(queued.isEmpty())return -1;int id=queued.remove();if(!owned.add(id))throw new AssertionError();info.set(0,1,1000000+id,flags.getOrDefault(id,0));return id;}
 public void releaseOutputBuffer(int id,boolean render){if(render||!owned.remove(id)||!released.add(id))throw new AssertionError("release "+id);}
 public Image getOutputImage(int id){if(!owned.contains(id))throw new AssertionError();imageCalls++;return nullImage?null:(lastImage=new Image(id));}
}
''')
 (p/'Check.java').write_text(r'''
package com.aurora.relay;
import android.media.*;import java.io.*;import java.util.concurrent.*;import java.util.concurrent.atomic.*;
class System {static long now=1000000000;static long nanoTime(){return now;}}
class SystemClock {static long elapsedRealtime(){return System.now/1000000;}}
public class Check {
 static class ScaledFrame {final byte[] pixels;final long receivedUs;ScaledFrame(byte[] p,long t){pixels=p;receivedUs=t;}}
 boolean peer=true,fail,previewEnabled;int scaleCalls,lastScaled;
 AtomicLong decoded=new AtomicLong(),decodeAgeUs=new AtomicLong(),videoDrops=new AtomicLong();
 FrameTiming decodeTiming=new FrameTiming();AdaptiveVideoRate videoRate=new AdaptiveVideoRate();
 AtomicReference<ScaledFrame> pendingImage=new AtomicReference<>();BlockingQueue<byte[]> freeImages=new ArrayBlockingQueue<>(3);
 Check(){videoRate.reset(SystemClock.elapsedRealtime());for(int i=0;i<3;i++)freeImages.add(new byte[8]);}
 boolean hasPeer(){return peer;}
 void scaleFast(Image image,byte[] nv)throws Exception{scaleCalls++;lastScaled=image.id;if(fail)throw new IOException("injected");}
 METHODS
 void consume(){ScaledFrame f=pendingImage.getAndSet(null);if(f!=null)check(freeImages.offer(f.pixels));}
 void outputs(VideoFrameSelector selector,MediaCodec codec,int...ids)throws Exception{
  for(int id:ids)codec.add(id);MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();int first=codec.dequeueOutputBuffer(info,1000);
  processVideoOutput(codec,info,first,new MediaCodec.BufferInfo(),selector);check(codec.owned.isEmpty());
 }
 static void check(boolean b){if(!b)throw new AssertionError();}
 public static void main(String[] args)throws Exception{
  Check c=new Check();VideoFrameSelector selector=new VideoFrameSelector();MediaCodec codec=new MediaCodec();
  c.outputs(selector,codec,0,1,2,3,4);check(c.scaleCalls==1&&c.lastScaled==4&&c.decoded.get()==5);
  check(c.pendingImage.get().receivedUs==1000004&&codec.lastImage.closed&&codec.released.size()==5);
  System.now+=10000000;c.outputs(selector,codec,5,6);check(c.scaleCalls==1&&codec.imageCalls==1);
  // A slow encoder already has an output waiting: skip before Image acquisition.
  System.now+=50000000;c.outputs(selector,codec,7);check(c.scaleCalls==1&&codec.imageCalls==1);
  c.consume();c.outputs(selector,codec,8);check(c.scaleCalls==2&&c.lastScaled==8);c.consume();
  c.peer=false;System.now+=100000000;c.outputs(selector,codec,9);check(c.scaleCalls==2);c.peer=true;
  c.outputs(selector,codec,10);check(c.scaleCalls==3);c.consume();
  // A foreground picture is produced before a 3DS connects, at a bounded 10Hz.
  Check preview=new Check();preview.peer=false;preview.previewEnabled=true;MediaCodec local=new MediaCodec();VideoFrameSelector localSelector=new VideoFrameSelector();
  preview.outputs(localSelector,local,100);check(preview.scaleCalls==1);preview.consume();
  System.now+=50_000_000;preview.outputs(localSelector,local,101);check(preview.scaleCalls==1);
  System.now+=50_000_000;preview.outputs(localSelector,local,102);check(preview.scaleCalls==2);preview.consume();
  preview.previewEnabled=false;System.now+=100_000_000;preview.outputs(localSelector,local,103);check(preview.scaleCalls==2);
  // Config buffers are released without replacing a real newer picture.
  System.now+=100000000;codec.flags.put(12,MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
  c.outputs(selector,codec,11,12);check(c.lastScaled==11);c.consume();
  // Failure must return pooled storage and release both Image and codec output.
  System.now+=100000000;c.fail=true;
  try{c.outputs(selector,codec,13);throw new AssertionError();}catch(IOException expected){}
  check(codec.owned.isEmpty()&&codec.lastImage.closed&&c.freeImages.size()==3);c.fail=false;
  System.now+=100000000;codec.nullImage=true;
  try{c.outputs(selector,codec,14);throw new AssertionError();}catch(IOException expected){}
  check(codec.owned.isEmpty()&&c.freeImages.size()==3);
  // Draining is bounded so compressed-input feeding cannot be starved.
  Check bounded=new Check();MediaCodec many=new MediaCodec();for(int i=0;i<12;i++)many.add(i);
  MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();int first=many.dequeueOutputBuffer(info,0);
  bounded.processVideoOutput(many,info,first,new MediaCodec.BufferInfo(),new VideoFrameSelector());
  check(bounded.lastScaled==8&&many.queued.size()==3&&many.owned.isEmpty());
  // Fractional cadence must not quantize 30fps delivery down to 20fps at 60Hz.
  for(double fps:new double[]{5,13.54,20,21.5,30}){
   VideoFrameSelector s=new VideoFrameSelector();long interval=(long)Math.ceil(1e9/fps);int count=0;
   for(int i=0;i<600;i++){long now=(long)(i*1e9/60);if(s.due(now,interval)){s.selected(now,interval);count++;}}
   check(Math.abs(count-fps*10)<=2);
  }
  VideoFrameSelector s=new VideoFrameSelector();s.selected(0,50000000);s.selected(1000000000,50000000);
  check(!s.due(1000000001,50000000));s.reset();check(s.due(1000000001,50000000));
 }
}
'''.replace('METHODS',methods))
 subprocess.run(['javac','-d',tmp,media/'Image.java',media/'MediaCodec.java',java/'VideoFrameSelector.java',java/'AdaptiveVideoRate.java',java/'Protocol.java',java/'FrameTiming.java',p/'Check.java'],check=True)
 subprocess.run(['java','-cp',tmp,'com.aurora.relay.Check'],check=True)
print('PASS production selective conversion: newest output, bounded drain, pre-scale skips, pooled-buffer/Image/codec ownership on errors, reconnect and fractional cadence')
