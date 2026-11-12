package com.aurora.relay;

import java.io.IOException;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;

/** Accept saved handheld reports while the app is open, independently of PlayStation streaming. */
final class IdleUsageRelay implements AutoCloseable {
 private static volatile IdleUsageRelay current;
 static volatile String listenerError="";
 static synchronized void start(){
  if(current!=null)return;
  try{current=new IdleUsageRelay(Protocol.PORT);current.worker.start();listenerError="";}
  catch(IOException ex){listenerError="Listener unavailable: "+ex.getClass().getSimpleName();}
 }
 static synchronized void stop(){IdleUsageRelay previous=current;current=null;if(previous!=null)previous.close();}
 static IdleUsageRelay instance(){return current;}
 final DatagramSocket socket;
 final int session=new SecureRandom().nextInt()|1;
 final Thread worker;
 final UsageProxy usage;
 volatile boolean running=true;
 volatile InetSocketAddress peer;
 volatile int clientRun,clientRelease;
 volatile long peerSeen;
 IdleUsageRelay(int port)throws SocketException {
  this(port,null);
 }
 IdleUsageRelay(int port,UsageProxy.Transport transport)throws SocketException {
  socket=new DatagramSocket(port);socket.setSoTimeout(100);
  UsageProxy.Reply reply=(run,serial,outcome)->{
   InetSocketAddress target=peer;if(!running||target==null||run!=clientRun)return;
   byte[] data=ByteBuffer.allocate(8).putInt(run).putInt(outcome).array();
   try{send(target,Protocol.USAGE_ACK,serial,data);}catch(IOException ignored){}
  };
  usage=transport==null?new UsageProxy(reply):new UsageProxy(transport,reply);
  worker=new Thread(this::network,"usage-relay-idle");worker.setDaemon(true);
 }
 static long now(){return System.nanoTime()/1000000;}
 boolean hasPeer(){return running&&peer!=null&&now()-peerSeen<4000;}
 private void send(InetSocketAddress target,int type,int stamp,byte[] data)throws IOException{
  int size=data==null?0:data.length;
  byte[] packet=Protocol.packet(type,session,type==Protocol.HELLO||type==Protocol.ERROR?Protocol.COMPAT:0,0,1,size,400,225,stamp,data,0,size);
  socket.send(new DatagramPacket(packet,packet.length,target));
 }
 private void link(InetSocketAddress target)throws IOException{send(target,Protocol.LINK,0,ByteBuffer.allocate(4).putInt(1).array());}
 void network(){
  byte[] buffer=new byte[Protocol.HEADER+Protocol.CHUNK];
  while(running)try{
   DatagramPacket incoming=new DatagramPacket(buffer,buffer.length);socket.receive(incoming);int size=incoming.getLength();
   if(!Protocol.valid(buffer,size))continue;
   ByteBuffer packet=ByteBuffer.wrap(buffer);int type=buffer[4]&255;
   InetSocketAddress source=new InetSocketAddress(incoming.getAddress(),incoming.getPort());
   if(type==Protocol.HELLO&&size==Protocol.HEADER){
    if(peer!=null&&!source.equals(peer)&&hasPeer())continue;
    if(!Protocol.compatibleHello(buffer,size)){
     if(source.equals(peer)){peer=null;usage.reset(clientRun);}
     byte[] message=("App versions do not match. Update both apps to release "+Protocol.RELEASE+" together.").getBytes(StandardCharsets.UTF_8);
     send(source,Protocol.ERROR,Protocol.RELEASE,message);continue;
    }
    int run=packet.getInt(28);
    if(peer==null||!source.equals(peer)||run!=clientRun){clientRun=run;usage.reset(run);}
    peer=source;clientRelease=Protocol.RELEASE;peerSeen=now();send(source,Protocol.HELLO,Protocol.RELEASE,null);link(source);continue;
   }
   if(peer==null||!source.equals(peer)||packet.getInt(8)!=session)continue;
   peerSeen=now();
   if(type==Protocol.USAGE_REPORT)usage.report(buffer,size);
   else if(type==Protocol.USAGE_CANCEL)usage.cancel(buffer,size);
   else if(type==Protocol.PING&&size==Protocol.HEADER){send(source,Protocol.PONG,packet.getInt(28),null);link(source);send(source,8,0,("{\"app_version\":"+Protocol.RELEASE+","+UsageDelivery.jsonFields()+"}").getBytes(StandardCharsets.UTF_8));}
   else if(type==Protocol.BYE)peer=null;
  }catch(SocketTimeoutException ignored){}catch(IOException error){if(running)listenerError="Listener error: "+error.getClass().getSimpleName();}
 }
 @Override public void close(){
  running=false;socket.close();usage.close();
  if(Thread.currentThread()!=worker)try{worker.join(300);}catch(InterruptedException error){Thread.currentThread().interrupt();}
 }
}
