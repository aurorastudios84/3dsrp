#!/usr/bin/env python3
"""Exercise the app-open report listener on real UDP sockets, without a PlayStation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];src=root/'android/app/src/main/java/com/aurora/relay'
java=r'''
package com.aurora.relay;
import java.net.*;import java.nio.*;import java.nio.charset.*;import java.util.concurrent.*;import java.util.concurrent.atomic.*;
public class IdleTest {
 static void check(boolean ok){if(!ok)throw new AssertionError();}
 static void send(DatagramSocket socket,int port,int type,int session,int serial,byte[] payload)throws Exception{
  int n=payload==null?0:payload.length;byte[] p=Protocol.packet(type,session,type==Protocol.HELLO?Protocol.COMPAT:0,0,1,n,0,0,serial,payload,0,n);
  socket.send(new DatagramPacket(p,p.length,InetAddress.getLoopbackAddress(),port));
 }
 static byte[] receive(DatagramSocket socket,int type)throws Exception{
  long end=System.nanoTime()+TimeUnit.SECONDS.toNanos(3);
  while(System.nanoTime()<end){DatagramPacket p=new DatagramPacket(new byte[1400],1400);socket.receive(p);if((p.getData()[4]&255)==type)return java.util.Arrays.copyOf(p.getData(),p.getLength());}throw new AssertionError("No packet type "+type);
 }
 static byte[] report(int run){
  byte[] payload=new byte[96];ByteBuffer b=ByteBuffer.wrap(payload);b.putInt(run);
  b.put("01234567-89ab-4def-8123-456789abcdef".getBytes(StandardCharsets.US_ASCII));b.put("11234567-89ab-4def-8123-456789abcdef".getBytes(StandardCharsets.US_ASCII));
  b.put("2026-11-06".getBytes(StandardCharsets.US_ASCII));b.put((byte)1).put((byte)0).putInt(92).putInt(1);return payload;
 }
 public static void main(String[] args)throws Exception{
  final UsageDelivery.Snapshot[] saved={null};UsageDelivery.restore(new UsageDelivery.Store(){public UsageDelivery.Snapshot load(){return null;}public void save(UsageDelivery.Snapshot snapshot){saved[0]=snapshot;}});
  AtomicInteger posts=new AtomicInteger();int port;int run=(int)0xf1234567;
  try(IdleUsageRelay relay=new IdleUsageRelay(0,json->{check(json.contains("\"app\":\"original_3ds\"")&&json.contains("\"seconds\":92"));posts.incrementAndGet();return 204;});DatagramSocket handheld=new DatagramSocket();DatagramSocket stranger=new DatagramSocket()){
   relay.worker.start();port=relay.socket.getLocalPort();handheld.setSoTimeout(1000);
   send(handheld,port,Protocol.HELLO,0,run,null);byte[] hello=receive(handheld,Protocol.HELLO);int session=ByteBuffer.wrap(hello).getInt(8);
   check(ByteBuffer.wrap(hello).getInt(12)==Protocol.COMPAT&&ByteBuffer.wrap(hello).getInt(28)==Protocol.RELEASE);
   check(ByteBuffer.wrap(receive(handheld,Protocol.LINK)).getInt(32)==1); // Console waiting: no PS5 is started.
   byte[] payload=report(run);send(handheld,port,Protocol.USAGE_REPORT,session,1,payload);
   byte[] ack=receive(handheld,Protocol.USAGE_ACK);check(ByteBuffer.wrap(ack).getInt(28)==1&&ByteBuffer.wrap(ack).getInt(32)==run&&ByteBuffer.wrap(ack).getInt(36)==1);
   check(posts.get()==1&&UsageDelivery.snapshot().confirmed==1);
   send(stranger,port,Protocol.USAGE_REPORT,session,2,payload);send(handheld,port,Protocol.USAGE_REPORT,session^1,3,payload);
   send(handheld,port,Protocol.PING,session,55,null);check(ByteBuffer.wrap(receive(handheld,Protocol.PONG)).getInt(28)==55);
   receive(handheld,Protocol.LINK);byte[] diagnostic=receive(handheld,8);String status=new String(diagnostic,32,diagnostic.length-32,StandardCharsets.UTF_8);check(status.contains("\"confirmed\":1")&&posts.get()==1);
  }
  check(saved[0]!=null&&saved[0].confirmed==1&&saved[0].status.equals("Cloudflare confirmed delivery")); // Retained after relay closes.
  try(DatagramSocket rebound=new DatagramSocket(port)){check(rebound.getLocalPort()==port);} // Streaming can acquire the port immediately.
  try(IdleUsageRelay relay=new IdleUsageRelay(port,json->{throw new UnknownHostException();});DatagramSocket handheld=new DatagramSocket()){
   relay.worker.start();handheld.setSoTimeout(1000);send(handheld,port,Protocol.HELLO,0,run,null);int session=ByteBuffer.wrap(receive(handheld,Protocol.HELLO)).getInt(8);receive(handheld,Protocol.LINK);
   byte[] payload=report(run);payload[4]='x';send(handheld,port,Protocol.USAGE_REPORT,session,1,payload);
   send(handheld,port,Protocol.PING,session,2,null);receive(handheld,Protocol.PONG);check(UsageDelivery.snapshot().rejected==1&&UsageDelivery.snapshot().status.contains("invalid anonymous ID"));
   send(handheld,port,Protocol.USAGE_REPORT,session,3,report(run));byte[] ack=receive(handheld,Protocol.USAGE_ACK);check(ByteBuffer.wrap(ack).getInt(36)==-1);
   check(UsageDelivery.snapshot().error.equals("DNS lookup failed")&&saved[0].failed==1);
  }
  check(UsageDelivery.snapshot().error.equals("DNS lookup failed"));
  for(int i=0;i<10;i++){try(IdleUsageRelay relay=new IdleUsageRelay(port,json->204)){relay.worker.start();}try(DatagramSocket rebound=new DatagramSocket(port)){}}
  System.out.println("PASS idle HELLO/source/heartbeat/usage ACK; no console dependency; wrong peer/session ignored; malformed report and DNS errors visible; diagnostics retained; port handoff/restart without leaked listeners");
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'IdleTest.java').write_text(java)
 subprocess.run(['javac','-d',tmp,*[str(src/(name+'.java')) for name in ['Protocol','UsageDelivery','UsageProxy','IdleUsageRelay']],str(p/'IdleTest.java')],check=True)
 subprocess.run(['java','-cp',tmp,'com.aurora.relay.IdleTest'],check=True)
