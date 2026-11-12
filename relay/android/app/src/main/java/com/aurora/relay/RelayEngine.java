package com.aurora.relay;
import android.media.*;
import android.graphics.*;
import android.os.*;
import android.util.Base64;
import java.net.*;
import java.io.*;
import java.nio.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

final class RelayEngine {
 static {System.loadLibrary("relay");}
 interface Listener {void message(String s);}
 volatile boolean previewEnabled;volatile byte[] previewJpeg;volatile long previewStamp;
 volatile int[] cropRequest={32768,32768,4};
 final SessionStatus messages;
 final ConsoleConnection consoleConnection=new ConsoleConnection();
 static final int SOURCE_WAITING=1,SOURCE_ACTIVE=2,SOURCE_TEST=3,SOURCE_FAILED=4,SOURCE_STOPPED=5;
 volatile int sourceState=SOURCE_WAITING;volatile boolean hadPeer;
 volatile String videoDecoder="Not started";
 volatile boolean running=true;
 final boolean pattern;
 final int quality;
 final int width,height;
 final StreamProfile profile;
 final long generation;
 DatagramSocket socket;
 volatile InetSocketAddress peer;
 volatile long peerSeen,controlSeen,lastDisplay,lastReport;
 volatile int clientRelease;volatile String compatibilityError="";volatile long clientShown,clientDecodeErrors,clientBadFrames;volatile int clientRun;
 final UsageProxy usageProxy=new UsageProxy((run,serial,outcome)->{
  InetSocketAddress target=peer;if(target==null||clientRun!=run||!running)return;
  byte[] data=ByteBuffer.allocate(8).putInt(run).putInt(outcome).array();
  byte[] packet=Protocol.packet(Protocol.USAGE_ACK,usageSession(),0,0,1,8,0,0,serial,data,0,8);
  try{socket.send(new DatagramPacket(packet,packet.length,target));}catch(IOException ignored){}
 });
 final int session=new java.security.SecureRandom().nextInt()|1;
 static final class VideoInput {final byte[] data;final long receivedUs;VideoInput(byte[] data){this.data=data;receivedUs=System.nanoTime()/1000;}}
 static final class ScaledFrame {final byte[] pixels;final long receivedUs;ScaledFrame(byte[] pixels,long receivedUs){this.pixels=pixels;this.receivedUs=receivedUs;}}
 final BlockingQueue<VideoInput> video=new ArrayBlockingQueue<>(32);
 final BlockingQueue<byte[]> audio=new ArrayBlockingQueue<>(32);
 final BlockingQueue<byte[]> freeImages=new ArrayBlockingQueue<>(3);
 final AtomicReference<ScaledFrame> pendingImage=new AtomicReference<>();
 final AdaptiveVideoRate videoRate=new AdaptiveVideoRate();
 final AtomicLong queueAgeUs=new AtomicLong(),decodeAgeUs=new AtomicLong(),sendAgeUs=new AtomicLong(),maxPhoneAgeUs=new AtomicLong();
 final List<Thread> threads=new ArrayList<>();
 volatile int audioRate,audioChannels,audioFrame;
 final AtomicLong pictures=new AtomicLong(),decoded=new AtomicLong(),sent=new AtomicLong(),bytes=new AtomicLong(),audioPackets=new AtomicLong(),videoDrops=new AtomicLong(),audioDrops=new AtomicLong(),jpegUs=new AtomicLong();
 final AtomicLong fullControls=new AtomicLong();
 final AtomicLong jpegBytes=new AtomicLong(),jpegMax=new AtomicLong(),jpegPackets=new AtomicLong(),jpegSendUs=new AtomicLong();
 final PcmPacketQueue pcmQueue=new PcmPacketQueue();
 final FrameTiming inputTiming=new FrameTiming(),decodeTiming=new FrameTiming(),jpegTiming=new FrameTiming(),soundTiming=new FrameTiming();
 final AtomicLong lostFrames=new AtomicLong();
 final AtomicLong scaleUs=new AtomicLong(),scaleCalls=new AtomicLong(),jpegCalls=new AtomicLong();
 boolean validateScaler;
 volatile long javaCheckUs,nativeCheckUs;volatile boolean scaleChecked,scaleNative=true;
 volatile long attachProbeNs,cachedProbeNs;
 volatile long rateStamp;volatile long rateInput,rateDecoded,rateSent;volatile double inputFps,decodedFps,sentFps;
 int vseq,aseq;
 RelayEngine(boolean test,int q,Listener l)throws Exception{
  this(test,q,new StreamProfile(StreamProfile.DEFAULT_HEIGHT,StreamProfile.DEFAULT_BITRATE),l);
 }
 RelayEngine(boolean test,int q,StreamProfile profile,Listener l)throws Exception{
  this(test,q,profile,new ImageSize(2),l);
 }
 RelayEngine(boolean test,int q,StreamProfile profile,ImageSize size,Listener l)throws Exception{
  width=size.width;height=size.height;
  this.profile=profile;pattern=test;sourceState=test?SOURCE_TEST:SOURCE_WAITING;quality=q;messages=new SessionStatus(l::message);generation=nativePrepare();videoRate.reset(SystemClock.elapsedRealtime());
  socket=new DatagramSocket(Protocol.PORT);socket.setSoTimeout(100);socket.setSendBufferSize(256*1024);
  for(int i=0;i<3;i++)freeImages.add(new byte[size.bytes()]);
 }
 void start(String ip,byte[] account,String pin,String pairFile){start(ip,account,pin,pairFile,true);}
 String failure(){return messages.failure();}
 void start(String ip,byte[] account,String pin,String pairFile,boolean ps5){
  spawn("relay-network",this::network);
  if(pattern){spawn("relay-pattern",this::testPattern);spawn("relay-tone",this::testTone);}
  else {
   spawn("relay-avc",this::decodeVideo);spawn("relay-jpeg",this::encodeVideo);spawn("relay-opus",this::decodeAudio);spawn("relay-pcm",this::sendAudio);
   spawn("relay-ps5",()->{
    if(!running)return;
    try {
     if(nativeHasPair(ip,account,pairFile,ps5)&&!ConsoleReadiness.prepare(ip,ps5,new ConsoleReadiness.Hooks(){
      public boolean running(){return running;}public void status(String s){onStatus(s);}public String wake(){return nativeWake(ip,account,pairFile,ps5);}
     }))return;
     consoleConnection.run(new ConsoleConnection.Hooks(){
      public boolean running(){return running&&failure()==null;}
      public String start(int attempt){return nativeStart(ip,account,attempt==1?pin:"",pairFile,generation,profile.height,profile.bitrate,profile.fps,ps5);}
      public void cleanup(){nativeStopAttempt(generation);}
      public void status(String text){onStatus(text);}
      public void failed(String text){failConnection(text);}
     });
    }catch(InterruptedException ex){Thread.currentThread().interrupt();}catch(Exception ex){if(running)failConnection("Console connection failed: "+ex.getMessage());}
   });
  }
 }
 void startFixtures(android.content.res.AssetManager assets){
  sourceState=SOURCE_TEST;validateScaler=true;
  spawn("jni-probe",()->{long[] p=nativeAttachmentProbe();if(p!=null){attachProbeNs=p[0]/p[2];cachedProbeNs=p[1]/p[2];}});
  spawn("relay-network",this::network);spawn("relay-avc",this::decodeVideo);spawn("relay-jpeg",this::encodeVideo);spawn("relay-opus",this::decodeAudio);spawn("relay-pcm",this::sendAudio);
  onAudioHeader(48000,2,480);
  spawn("fixture-video",()->feedFixture(assets,"fixture-video.bin",1000/profile.fps,true));
  spawn("fixture-audio",()->feedFixture(assets,"fixture-audio.bin",10,false));
 }
 void feedFixture(android.content.res.AssetManager assets,String name,int ms,boolean isVideo){
  try {while(running){DataInputStream in=new DataInputStream(assets.open(name));int count=in.readInt();for(int i=0;i<count&&running;i++){int size=in.readInt();if(size<1||size>2*1024*1024)throw new IOException("Fixture size");byte[] b=new byte[size];in.readFully(b);if(isVideo)onVideo(b);else onAudio(b);sleep(ms);}in.close();}}
  catch(IOException ex){onStatus("Fixture: "+ex.getMessage());}
 }
 synchronized void spawn(String name,Runnable r){Thread t=new Thread(()->{try{r.run();}catch(Exception ex){if(running)failConnection(name+" failed: "+ex.getClass().getSimpleName()+" "+ex.getMessage());}},name);threads.add(t);t.start();}
 void onStatus(String text){
  if(text.startsWith("Video decode failed")||text.startsWith("Sound decode failed"))failConnection(text);
  else messages.update(text);
 }
 void onConsoleConnected(){sourceState=SOURCE_ACTIVE;sendLinkState();consoleConnection.connected();}
 void onConsoleQuit(String text,boolean retryable){sourceState=SOURCE_WAITING;sendLinkState();consoleConnection.quit(text,retryable);}
 void failConnection(String text){sourceState=SOURCE_FAILED;sendLinkState();messages.fail(text);}
 int usageSession(){return session;}
 void sendLinkState(){
  byte[] state=ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN).putInt(sourceState).array();
  try{send(Protocol.LINK,0,0,1,4,0,0,(int)SystemClock.elapsedRealtime(),state,0,4);}catch(IOException ignored){}
 }
 boolean onVideo(byte[] b,int lost){if(lost>0)lostFrames.addAndGet(lost);return onVideo(b);}
 boolean onVideo(byte[] b){inputTiming.event(System.nanoTime()/1000);pictures.incrementAndGet();boolean ok=video.offer(new VideoInput(b));if(!ok)videoDrops.incrementAndGet();return ok;}
 void onAudio(byte[] b){if(!audio.offer(b))audioDrops.incrementAndGet();}
 void onAudioHeader(int rate,int channels,int frame){audioRate=rate;audioChannels=channels;audioFrame=frame;}
 boolean hasPeer(){return compatibilityError.isEmpty()&&peer!=null&&SystemClock.elapsedRealtime()-peerSeen<3000;}
 void send(int type,int seq,int index,int fragments,int total,int w,int h,int stamp,byte[] data,int offset,int length)throws IOException{
  InetSocketAddress target=peer;if(target==null)return;
  byte[] b=Protocol.packet(type,session,seq,index,fragments,total,w,h,stamp,data,offset,length);
  socket.send(new DatagramPacket(b,b.length,target));bytes.addAndGet(b.length);
 }
 void network(){
  byte[] buf=new byte[Protocol.HEADER+Protocol.CHUNK];boolean neutral=true,haveInput=false;int lastInput=0;
  while(running){
   // Check before receive: invalid/stale traffic must not bypass release retries.
   if(!neutral&&(peer==null||SystemClock.elapsedRealtime()-controlSeen>500))
    neutral=pattern||nativeControls(0,0,0,0,0);
   try {
    DatagramPacket p=new DatagramPacket(buf,buf.length);socket.receive(p);
    int n=p.getLength();if(!Protocol.valid(buf,n))continue;
    ByteBuffer b=ByteBuffer.wrap(buf,0,n).order(ByteOrder.BIG_ENDIAN);int type=buf[4]&255,id=b.getInt(8);
    InetSocketAddress source=new InetSocketAddress(p.getAddress(),p.getPort());
    if(type==Protocol.HELLO&&n==Protocol.HEADER){
     if(peer!=null&&!source.equals(peer)&&hasPeer())continue;
     clientRelease=(b.getInt(12)&0xffff0000)==0x52330000?b.getInt(12)&0xffff:0;
     if(!Protocol.compatibleHello(buf,n)){
      compatibilityError="App versions do not match. Update both apps to release "+Protocol.RELEASE+" together.";
      if(source.equals(peer)){peer=null;neutral=pattern||nativeControls(0,0,0,0,0);}
      byte[] msg=compatibilityError.getBytes(java.nio.charset.StandardCharsets.UTF_8);
      byte[] error=Protocol.packet(Protocol.ERROR,session,Protocol.COMPAT,0,1,msg.length,width,height,BuildConfig.VERSION_CODE,msg,0,msg.length);socket.send(new DatagramPacket(error,error.length,source));onStatus(compatibilityError);continue;
     }
     compatibilityError="";int run=b.getInt(28);
     if(peer==null||!source.equals(peer)||run!=clientRun){onStatus("3DS connected");videoRate.reset(SystemClock.elapsedRealtime());haveInput=false;lastDisplay=lastReport=clientShown=clientDecodeErrors=clientBadFrames=0;cropRequest=new int[]{32768,32768,4};clientRun=run;usageProxy.reset(run);}
     hadPeer=true;peer=source;peerSeen=SystemClock.elapsedRealtime();send(Protocol.HELLO,Protocol.COMPAT,0,1,0,width,height,Protocol.RELEASE,null,0,0);sendLinkState();continue;
    }
    if(peer==null||!source.equals(peer)||id!=session)continue;
    if(type==Protocol.USAGE_REPORT){usageProxy.report(buf,n);continue;}
    if(type==Protocol.USAGE_CANCEL){usageProxy.cancel(buf,n);continue;}
    if(type==Protocol.STATUS&&n==Protocol.HEADER+12&&b.getInt(20)==12&&b.getShort(16)==0&&b.getShort(18)==1){long count=b.getInt(32)&0xffffffffL;long now=SystemClock.elapsedRealtime();if(count!=clientShown&&count>0)lastDisplay=now;clientShown=count;clientDecodeErrors=b.getInt(36)&0xffffffffL;clientBadFrames=b.getInt(40)&0xffffffffL;lastReport=peerSeen=now;}
    if(type==Protocol.FEEDBACK)videoRate.feedback(buf,n,SystemClock.elapsedRealtime());
    if(type==Protocol.PING&&n==Protocol.HEADER){peerSeen=SystemClock.elapsedRealtime();send(Protocol.PONG,b.getInt(12),0,1,0,0,0,b.getInt(28),null,0,0);sendDiagnostics();sendLinkState();}
    if(type==Protocol.INPUT&&n==Protocol.HEADER+10&&!pattern){int inputSeq=b.getInt(28);if(haveInput&&inputSeq-lastInput<=0)continue;if(nativeControls(b.getInt(32),b.getShort(36),b.getShort(38),buf[40]&255,buf[41]&255)){lastInput=inputSeq;haveInput=true;peerSeen=controlSeen=SystemClock.elapsedRealtime();neutral=false;}}
    if(type==Protocol.INPUT_FULL&&n==Protocol.HEADER+68&&b.getInt(20)==68&&b.getShort(16)==0&&b.getShort(18)==1&&!pattern){int inputSeq=b.getInt(28);if(haveInput&&inputSeq-lastInput<=0)continue;if(nativeFullControls(buf,Protocol.HEADER)){fullControls.incrementAndGet();lastInput=inputSeq;haveInput=true;peerSeen=controlSeen=SystemClock.elapsedRealtime();neutral=false;}}
    if(type==10&&n==Protocol.HEADER+6&&b.getInt(20)==6&&b.getShort(16)==0&&b.getShort(18)==1){
     int z=b.getShort(36)&65535;if(z>=4&&z<=32)cropRequest=new int[]{b.getShort(32)&65535,b.getShort(34)&65535,z};
    }
    if(type==Protocol.BYE){peer=null;videoRate.reset(SystemClock.elapsedRealtime());neutral=pattern||nativeControls(0,0,0,0,0);onStatus("3DS stream closed");}
   }catch(SocketTimeoutException ignored){}catch(IOException ex){if(running)onStatus("Relay socket: "+ex.getMessage());}
  }
 }
 void decodeVideo(){
  MediaCodec codec=null;
  try {
   MediaFormat f=MediaFormat.createVideoFormat("video/avc",profile.width,profile.height);
   f.setInteger(MediaFormat.KEY_COLOR_FORMAT,MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
   f.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE,2*1024*1024);
   if(Build.VERSION.SDK_INT>=30)f.setInteger("low-latency",1);
   codec=MediaCodec.createDecoderByType("video/avc");codec.configure(f,null,null,0);codec.start();
   videoDecoder=codec.getName();
   VideoInput pending=null;long pts=0;MediaCodec.BufferInfo info=new MediaCodec.BufferInfo(),newer=new MediaCodec.BufferInfo();VideoFrameSelector selector=new VideoFrameSelector();
   while(running){
    if(pending==null)pending=video.poll();
    if(pending!=null){int in=codec.dequeueInputBuffer(0);if(in>=0){ByteBuffer dest=codec.getInputBuffer(in);if(dest==null||dest.capacity()<pending.data.length)throw new IOException("AVC buffer too small");dest.clear();dest.put(pending.data);pts=Math.max(pts+1,pending.receivedUs);queueAgeUs.set(Math.max(0,System.nanoTime()/1000-pending.receivedUs));codec.queueInputBuffer(in,0,pending.data.length,pts,0);pending=null;}}
    int out=codec.dequeueOutputBuffer(info,1000);
    if(out>=0){
     processVideoOutput(codec,info,out,newer,selector);
    }else if(pending==null&&video.isEmpty())Thread.sleep(1);
   }
  }catch(Exception ex){if(running)onStatus("Video decode failed: "+ex.getMessage());}
  finally{if(codec!=null){try{codec.stop();}catch(Exception ignored){}codec.release();}}
 }
 void observeVideoOutput(MediaCodec.BufferInfo info){
  if((info.flags&MediaCodec.BUFFER_FLAG_CODEC_CONFIG)!=0)return;
  decodeTiming.event(System.nanoTime()/1000);decoded.incrementAndGet();
  decodeAgeUs.set(Math.max(0,System.nanoTime()/1000-info.presentationTimeUs));
 }
 // Own every dequeued output until it is released, including on conversion failure.
 void processVideoOutput(MediaCodec codec,MediaCodec.BufferInfo info,int output,MediaCodec.BufferInfo newer,VideoFrameSelector selector)throws Exception{
  int held=output;long outputStart=System.nanoTime();
  try {
   observeVideoOutput(info);
   // Bounded draining leaves time to feed compressed inputs on the next loop.
   for(int i=0;i<8;i++){
    int candidate=codec.dequeueOutputBuffer(newer,0);if(candidate<0)break;
    observeVideoOutput(newer);
    if((newer.flags&MediaCodec.BUFFER_FLAG_CODEC_CONFIG)!=0){codec.releaseOutputBuffer(candidate,false);continue;}
    int previous=held;held=candidate;
    codec.releaseOutputBuffer(previous,false);videoDrops.incrementAndGet();
    info.set(newer.offset,newer.size,newer.presentationTimeUs,newer.flags);
   }
   if(!hasPeer()&&!previewEnabled){selector.reset();return;}
   if((info.flags&MediaCodec.BUFFER_FLAG_CODEC_CONFIG)!=0)return;
   long now=System.nanoTime(),interval=hasPeer()?videoRate.intervalNs(SystemClock.elapsedRealtime()):100_000_000L;
   if(!selector.due(now,interval)||pendingImage.get()!=null){videoDrops.incrementAndGet();return;}
   byte[] nv=freeImages.poll();if(nv==null){videoDrops.incrementAndGet();return;}
   boolean published=false;
   try {
    android.media.Image image=codec.getOutputImage(held);
    if(image==null)throw new IOException("This phone decoder does not expose YUV images");
    try {selector.selected(now,interval);scaleFast(image,nv);}finally{image.close();}
    ScaledFrame old=pendingImage.getAndSet(new ScaledFrame(nv,info.presentationTimeUs));published=true;
    if(old!=null){freeImages.offer(old.pixels);videoDrops.incrementAndGet();}
   }finally{if(!published)freeImages.offer(nv);}
  }finally{codec.releaseOutputBuffer(held,false);decodeTiming.work((System.nanoTime()-outputStart)/1000);}
 }
 void scaleFast(android.media.Image image,byte[] nv){
  long scaleStart=System.nanoTime();
  Rect crop=image.getCropRect();
  int[] request=cropRequest;
  if(request[2]>4){
   Rect full=crop;
   int cw=Math.max(2,(full.width()*4/request[2])&~1),ch=Math.max(2,(full.height()*4/request[2])&~1);
   int x=(full.left+(int)((long)(full.width()-cw)*request[0]/65535))&~1;
   int y=(full.top+(int)((long)(full.height()-ch)*request[1]/65535))&~1;
   x=Math.max(full.left,x);y=Math.max(full.top,y);
   crop=new Rect(x,y,x+cw,y+ch);
  }
  android.media.Image.Plane[] p=image.getPlanes();int[] layout=new int[13];
  for(int i=0;i<3;i++){layout[i*3]=p[i].getBuffer().position();layout[i*3+1]=p[i].getRowStride();layout[i*3+2]=p[i].getPixelStride();}
  layout[9]=crop.left;layout[10]=crop.top;layout[11]=crop.width();layout[12]=crop.height();
  long before=System.nanoTime();boolean ok=scaleNative&&nativeScale(p[0].getBuffer(),p[1].getBuffer(),p[2].getBuffer(),layout,nv,width,height);
  long elapsed=(System.nanoTime()-before)/1000;
  if(!scaleChecked&&validateScaler){
   byte[] reference=new byte[nv.length];before=System.nanoTime();scale(image,reference,crop);javaCheckUs=(System.nanoTime()-before)/1000;nativeCheckUs=elapsed;
   if(!ok||!Arrays.equals(nv,reference)){scaleNative=false;System.arraycopy(reference,0,nv,0,nv.length);}scaleChecked=true;
  }else if(!ok){scaleNative=false;scale(image,nv,crop);}
  scaleUs.addAndGet((System.nanoTime()-scaleStart)/1000);scaleCalls.incrementAndGet();
 }
 void scale(android.media.Image image,byte[] nv){scale(image,nv,image.getCropRect());}
 void scale(android.media.Image image,byte[] nv,Rect crop){
  android.media.Image.Plane[] planes=image.getPlanes();
  if(planes.length!=3||crop.width()<2||crop.height()<2)throw new IllegalArgumentException("Unsupported YUV layout");
  for(int p=0;p<3;p++){
   int w=p==0?width:width/2,h=p==0?height:(height+1)/2,div=p==0?1:2;
   ByteBuffer src=planes[p].getBuffer();int base=src.position(),row=planes[p].getRowStride(),pixel=planes[p].getPixelStride();
   int sw=crop.width()/div,sh=crop.height()/div;
   for(int y=0;y<h;y++)for(int x=0;x<w;x++){
    int xa=x*sw,xb=(x+1)*sw,ya=y*sh,yb=(y+1)*sh;long sum=0;
    for(int sy=ya/h;sy<(yb+h-1)/h;sy++){
     int wy=Math.min(yb,(sy+1)*h)-Math.max(ya,sy*h);
     for(int sx=xa/w;sx<(xb+w-1)/w;sx++){
      int wx=Math.min(xb,(sx+1)*w)-Math.max(xa,sx*w);
      sum+=(long)(src.get(base+(crop.top/div+sy)*row+(crop.left/div+sx)*pixel)&255)*wx*wy;
     }
    }
    int dst=p==0?y*width+x:width*height+y*width+x*2+(p==1?1:0);
    nv[dst]=(byte)((sum+(long)sw*sh/2)/((long)sw*sh));
   }
  }
 }
 private native byte[] nativeJpeg(byte[] nv,int quality,int width,int height);
 void encodeVideo(){
  long next=0;
  while(running){
   if(!hasPeer()&&!previewEnabled){ScaledFrame old=pendingImage.getAndSet(null);if(old!=null)freeImages.offer(old.pixels);next=0;sleep(5);continue;}
   long now=SystemClock.elapsedRealtime();if(now<next){sleep(Math.min(5,next-now));continue;}
   ScaledFrame frame=pendingImage.getAndSet(null);if(frame==null){sleep(1);continue;}
   byte[] nv=frame.pixels;
   try {next=now+(hasPeer()?videoRate.intervalMs(now):100);
    long start=System.nanoTime();byte[] jpeg=nativeJpeg(nv,quality,width,height);
    if(jpeg==null)throw new IOException("JPEG encoder failed");
    jpegUs.addAndGet((System.nanoTime()-start)/1000);jpegCalls.incrementAndGet();if(previewEnabled&&SystemClock.elapsedRealtime()-previewStamp>=100){previewJpeg=jpeg;previewStamp=SystemClock.elapsedRealtime();}sendJpeg(jpeg);if(hasPeer()){long age=Math.max(0,System.nanoTime()/1000-frame.receivedUs);sendAgeUs.set(age);maxPhoneAgeUs.accumulateAndGet(age,Math::max);}jpegTiming.work((System.nanoTime()-start)/1000);
   }catch(Exception ex){if(running)onStatus("JPEG relay: "+ex.getMessage());}finally{freeImages.offer(nv);}
  }
 }
 void sendJpeg(byte[] jpeg)throws IOException{
  if(!hasPeer())return;if(jpeg.length>Protocol.MAX_JPEG){videoDrops.incrementAndGet();return;}
  int count=(jpeg.length+Protocol.CHUNK-1)/Protocol.CHUNK,seq=++vseq,stamp=(int)SystemClock.elapsedRealtime();
  VideoPacketPacer pacer=new VideoPacketPacer(count);long begin=System.nanoTime();
  for(int i=0;i<count;i++){
   if(!pacer.await(()->running))return;
   int off=i*Protocol.CHUNK;send(Protocol.JPEG,seq,i,count,jpeg.length,width,height,stamp,jpeg,off,Math.min(Protocol.CHUNK,jpeg.length-off));
   pacer.sent();jpegPackets.incrementAndGet();
  }
  jpegSendUs.addAndGet((System.nanoTime()-begin)/1000);jpegBytes.addAndGet(jpeg.length);jpegMax.accumulateAndGet(jpeg.length,Math::max);
  jpegTiming.event(System.nanoTime()/1000);sent.incrementAndGet();
 }
 void decodeAudio(){
  MediaCodec codec=null;
  try{
   while(running&&audioRate==0)sleep(5);if(!running)return;
   MediaFormat f=MediaFormat.createAudioFormat("audio/opus",audioRate,audioChannels);
   f.setInteger(MediaFormat.KEY_PCM_ENCODING,AudioFormat.ENCODING_PCM_16BIT);
   ByteBuffer head=ByteBuffer.allocate(19).order(ByteOrder.LITTLE_ENDIAN);head.put(new byte[]{'O','p','u','s','H','e','a','d'}).put((byte)1).put((byte)audioChannels).putShort((short)0).putInt(audioRate).putShort((short)0).put((byte)0);head.flip();
   ByteBuffer delay=ByteBuffer.allocate(8).order(ByteOrder.nativeOrder());delay.putLong(0);delay.flip();
   ByteBuffer preroll=ByteBuffer.allocate(8).order(ByteOrder.nativeOrder());preroll.putLong(0);preroll.flip();
   f.setByteBuffer("csd-0",head);f.setByteBuffer("csd-1",delay);f.setByteBuffer("csd-2",preroll);
   codec=MediaCodec.createDecoderByType("audio/opus");codec.configure(f,null,null,0);codec.start();
   int rate=audioRate,channels=audioChannels;PcmResampler resampler=new PcmResampler(rate,channels);long pts=0;byte[] pending=null;MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();
   while(running){
    if(pending==null)pending=audio.poll();
    if(pending!=null){int in=codec.dequeueInputBuffer(0);if(in>=0){ByteBuffer dest=codec.getInputBuffer(in);if(dest==null||dest.capacity()<pending.length)throw new IOException("Opus buffer too small");dest.clear();dest.put(pending);codec.queueInputBuffer(in,0,pending.length,pts,0);pts+=audioFrame*1000000L/audioRate;pending=null;}}
    int out=codec.dequeueOutputBuffer(info,1000);
    if(out==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED){MediaFormat format=codec.getOutputFormat();rate=format.getInteger(MediaFormat.KEY_SAMPLE_RATE);channels=format.getInteger(MediaFormat.KEY_CHANNEL_COUNT);resampler=new PcmResampler(rate,channels);}
    if(out>=0){try{ByteBuffer pcm=codec.getOutputBuffer(out);if(pcm!=null&&hasPeer()&&info.size>0){pcm.position(info.offset);pcm.limit(info.offset+info.size);pcm=pcm.slice().order(ByteOrder.LITTLE_ENDIAN);pcmQueue.offer(resampler.convert(pcm));}}finally{codec.releaseOutputBuffer(out,false);}}
    else if(pending==null&&audio.isEmpty())sleep(1);
   }
  }catch(Exception ex){if(running)onStatus("Sound decode failed: "+ex.getMessage());}
  finally{if(codec!=null){try{codec.stop();}catch(Exception ignored){}codec.release();}}
 }
 void sendAudio(){
  byte[] packet=new byte[PcmPacketQueue.BYTES];
  while(running){
   if(!hasPeer()){pcmQueue.clear();sleep(5);continue;}
   long now=System.nanoTime()/1000;
   if(pcmQueue.poll(now,packet)){try{sendPcm(packet,packet.length);}catch(IOException ex){if(running)onStatus("Sound send failed: "+ex.getMessage());}}
   else java.util.concurrent.locks.LockSupport.parkNanos(Math.max(100,pcmQueue.waitUs(now))*1000);
  }
 }
 void sendPcm(byte[] data,int length)throws IOException{
  soundTiming.event(System.nanoTime()/1000);long soundStart=System.nanoTime();
  for(int off=0;off<length;off+=960){int size=Math.min(960,length-off);send(Protocol.PCM,++aseq,0,1,size,12000,1,(int)SystemClock.elapsedRealtime(),data,off,size);audioPackets.incrementAndGet();}
  soundTiming.work((System.nanoTime()-soundStart)/1000);
 }
 void testPattern(){
  Bitmap image=Bitmap.createBitmap(width,height,Bitmap.Config.RGB_565);Canvas canvas=new Canvas(image);Paint paint=new Paint();paint.setTextSize(22);int frame=0;
  while(running){long start=SystemClock.elapsedRealtime();canvas.drawColor(Color.rgb((frame*3)%180,30,70));paint.setColor(Color.YELLOW);canvas.drawRect((frame*5)%(width-40),70,(frame*5)%(width-40)+40,110,paint);paint.setColor(Color.WHITE);canvas.drawText("Relay "+frame++,12,35,paint);
   try{ByteArrayOutputStream out=new ByteArrayOutputStream();image.compress(Bitmap.CompressFormat.JPEG,quality,out);byte[] jpeg=out.toByteArray();if(previewEnabled){previewJpeg=jpeg;previewStamp=SystemClock.elapsedRealtime();}sendJpeg(jpeg);}catch(IOException ex){if(running)onStatus(ex.getMessage());}sleep(Math.max(1,videoRate.intervalMs(SystemClock.elapsedRealtime())-(SystemClock.elapsedRealtime()-start)));}
  image.recycle();
 }
 void testTone(){long sample=0;while(running){byte[] pcm=new byte[480];for(int i=0;i<240;i++){short value=(short)(Math.sin(2*Math.PI*440*sample++/12000)*1800);pcm[2*i]=(byte)value;pcm[2*i+1]=(byte)(value>>8);}try{if(hasPeer())sendPcm(pcm,pcm.length);}catch(IOException ignored){}sleep(20);}}
 static void sleep(long ms){try{Thread.sleep(ms);}catch(InterruptedException ignored){}}
 synchronized void updateRates(){long now=SystemClock.elapsedRealtime(),a=pictures.get(),b=decoded.get(),c=sent.get();if(rateStamp!=0&&now-rateStamp<1000)return;if(rateStamp!=0){double seconds=(now-rateStamp)/1000.0;inputFps=(a-rateInput)/seconds;decodedFps=(b-rateDecoded)/seconds;sentFps=(c-rateSent)/seconds;}rateStamp=now;rateInput=a;rateDecoded=b;rateSent=c;}
 String phoneAges(){return "["+queueAgeUs.get()+","+decodeAgeUs.get()+","+sendAgeUs.get()+","+maxPhoneAgeUs.get()+","+video.size()+"]";}
 String diagnostics(){updateRates();long[] bridge=nativeBridgeStats();return "{\"app_version\":"+BuildConfig.VERSION_CODE+",\"image_size\":["+width+","+height+"],\"ps5_request\":"+profile.json()+",\"core_backend\":\"chiaki-ng-v1.10.0\",\"bridge\":"+Arrays.toString(bridge)+",\"connection\":"+Arrays.toString(nativeConnection())+",\"source_audio\":["+audioRate+","+audioChannels+","+audioFrame+"]"+",\"reported_lost\":"+lostFrames.get()+",\"input_fps\":"+(Math.round(inputFps*10)/10.0)+",\"decoded_fps\":"+(Math.round(decodedFps*10)/10.0)+",\"sent_fps\":"+(Math.round(sentFps*10)/10.0)+",\"input\":"+pictures.get()+",\"decoded\":"+decoded.get()+",\"sent\":"+sent.get()+",\"scale_native\":"+scaleNative+",\"java_check_us\":"+javaCheckUs+",\"native_check_us\":"+nativeCheckUs+",\"scale_calls\":"+scaleCalls.get()+",\"scale_us\":"+scaleUs.get()+",\"jpeg_calls\":"+jpegCalls.get()+",\"jpeg_us\":"+jpegUs.get()+",\"timing_us\":["+inputTiming.snapshot()+","+decodeTiming.snapshot()+","+jpegTiming.snapshot()+","+soundTiming.snapshot()+"],\"video_dropped\":"+videoDrops.get()+",\"jpeg_delivery\":["+quality+","+jpegBytes.get()+","+jpegMax.get()+","+jpegPackets.get()+","+jpegSendUs.get()+"],\"full_controls\":"+fullControls.get()+",\"adaptive\":"+videoRate.snapshot(SystemClock.elapsedRealtime())+",\"phone_age_us\":"+phoneAges()+",\"pcm_queue\":"+pcmQueue.snapshot()+",\"audio_dropped\":"+audioDrops.get()+"}";}
 // Preserve rate/age evidence even when long-running counters outgrow one datagram.
 String compactDiagnostics(){return "{\"app_version\":"+BuildConfig.VERSION_CODE+",\"image_size\":["+width+","+height+"],\"input_fps\":"+inputFps+",\"decoded_fps\":"+decodedFps+",\"sent_fps\":"+sentFps+",\"adaptive\":"+videoRate.snapshot(SystemClock.elapsedRealtime())+",\"phone_age_us\":"+phoneAges()+",\"video_dropped\":"+videoDrops.get()+",\"audio_dropped\":"+audioDrops.get()+",\"compact\":true}";}
 static String withUsage(String json){return json.substring(0,json.length()-1)+","+UsageDelivery.jsonFields()+"}";}
 void sendDiagnostics()throws IOException{byte[] b=withUsage(diagnostics()).getBytes(java.nio.charset.StandardCharsets.UTF_8);if(b.length>Protocol.CHUNK)b=withUsage(compactDiagnostics()).getBytes(java.nio.charset.StandardCharsets.UTF_8);if(b.length<=Protocol.CHUNK)send(8,0,0,1,b.length,0,0,0,b,0,b.length);}

 void stop(){
  running=false;usageProxy.close();messages.close();sourceState=SOURCE_STOPPED;sendLinkState();socket.close();nativeStop();
  List<Thread> workers;synchronized(this){workers=new ArrayList<>(threads);}
  for(Thread t:workers)if(t!=Thread.currentThread())t.interrupt();
  for(Thread t:workers)if(t!=Thread.currentThread())try{t.join(2000);}catch(InterruptedException ex){Thread.currentThread().interrupt();break;}
 }
 static native String nativeWake(String host,byte[] account,String path,boolean ps5);
 native boolean nativeRest();
 static native boolean nativeHasPair(String ip,byte[] account,String path,boolean ps5);
 private native String nativeStart(String ip,byte[] account,String pin,String pairFile,long generation,int height,int bitrate,int fps,boolean ps5);
 private native long nativePrepare();
 private native long[] nativeAttachmentProbe();
 private native long[] nativeBridgeStats();
 private native long[] nativeConnection();
 private native boolean nativeScale(ByteBuffer y,ByteBuffer u,ByteBuffer v,int[] layout,byte[] output,int width,int height);
 private native void nativeStop();
 private native void nativeStopAttempt(long generation);
 private native boolean nativeFullControls(byte[] packet,int offset);
 private native boolean nativeControls(int buttons,int lx,int ly,int l2,int r2);
}
