#!/usr/bin/env python3
"""Validate status packets, loss/recovery states and Android's task-close lifecycle callback."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.c').write_text(r'''
#include <assert.h>
#include <string.h>
#include "link_state.h"
int main(){
 uint8_t packet[RELAY_HEADER+4];relay_header(packet,RELAY_LINK,7,0,0,1,4,0,0,0);w32(packet+RELAY_HEADER,RELAY_SOURCE_ACTIVE);
 assert(relay_source_packet(packet,sizeof(packet))==RELAY_SOURCE_ACTIVE);
 for(unsigned n=0;n<sizeof(packet);n++)assert(relay_source_packet(packet,n)==-1);
 w32(packet+RELAY_HEADER,99);assert(relay_source_packet(packet,sizeof(packet))==-1);w32(packet+RELAY_HEADER,RELAY_SOURCE_STOPPED);
 assert(relay_source_packet(packet,sizeof(packet))==RELAY_SOURCE_STOPPED);
 w16(packet+16,1);assert(relay_source_packet(packet,sizeof(packet))==-1);w16(packet+16,0);
 w32(packet+20,3);assert(relay_source_packet(packet,sizeof(packet))==-1);
 assert(relay_connection_view(false,0,0,false,false,false)==RELAY_VIEW_WAIT_PHONE);
 assert(relay_connection_view(false,3,RELAY_SOURCE_ACTIVE,true,true,true)==RELAY_VIEW_PHONE_LOST);
 assert(relay_connection_view(true,0,RELAY_SOURCE_STOPPED,true,true,true)==RELAY_VIEW_PHONE_LOST);
 assert(relay_connection_view(true,0,RELAY_SOURCE_WAITING,true,false,false)==RELAY_VIEW_WAIT_PS5);
 assert(relay_connection_view(true,0,RELAY_SOURCE_ACTIVE,true,false,false)==RELAY_VIEW_WAIT_VIDEO);
 assert(relay_connection_view(true,0,RELAY_SOURCE_ACTIVE,true,true,true)==RELAY_VIEW_LIVE);
 assert(relay_connection_view(true,0,RELAY_SOURCE_ACTIVE,true,true,false)==RELAY_VIEW_VIDEO_STALLED);
 assert(relay_connection_view(true,0,RELAY_SOURCE_ACTIVE,false,true,true)==RELAY_VIEW_WAIT_PS5);
 assert(relay_connection_view(true,0,RELAY_SOURCE_TEST,true,true,true)==RELAY_VIEW_LIVE);
 assert(relay_connection_view(true,0,RELAY_SOURCE_FAILED,true,true,true)==RELAY_VIEW_SOURCE_FAILED);
 assert(relay_connection_view(false,1,0,false,true,true)==RELAY_VIEW_VERSION);
 // Startup -> active -> killed phone -> reconnect waiting -> active, including modular time wrap.
 uint32_t seen=UINT32_MAX-100;
 assert(!relay_link_expired(seen+1000,seen));assert(relay_link_expired(seen+4001,seen));
 assert(!relay_link_expired(100,101));
 assert(strcmp(relay_view_title(RELAY_VIEW_PHONE_LOST),"Relay disconnected")==0);
}
''')
 subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I'+str(r/'common'),p/'check.c','-o',p/'check'],check=True);subprocess.run([p/'check'],check=True)
 service=(r/'android/app/src/main/java/com/aurora/relay/RelayService.java').read_text()
 callback=service[service.index(' @Override public void onTaskRemoved('):service.index(' @Override public void onDestroy(')]
 engine=(r/'android/app/src/main/java/com/aurora/relay/RelayEngine.java').read_text()
 heartbeat=engine[engine.index(' void sendLinkState(){'):engine.index(' boolean onVideo(')]
 shutdown=engine[engine.index(' void stop(){'):engine.index(' static native String nativeWake(')]
 (p/'Check.java').write_text('''package com.aurora.relay;
import java.nio.*;import java.io.*;import java.util.*;
class Intent {}
class Base {int callbacks;public void onTaskRemoved(Intent i){callbacks++;}}
class SystemClock {static long elapsedRealtime(){return 1234;}}
class EngineCheck {
 static final int SOURCE_STOPPED=5;int sourceState=2,nativeStops;boolean running=true,failSend;
 UsageProxy usageProxy=new UsageProxy((r,s,v)->{});
 SessionStatus messages=new SessionStatus(s->{});List<Thread> threads=new ArrayList<>();byte[] packet;
 class Socket {boolean closed;void close(){closed=true;}}Socket socket=new Socket();
 void send(int type,int seq,int index,int count,int length,int width,int height,int stamp,byte[] data,int offset,int size)throws IOException {
  assert !socket.closed;if(failSend)throw new IOException();packet=Protocol.packet(type,7,seq,index,count,length,width,height,stamp,data,offset,size);
 }
 void nativeStop(){assert socket.closed;nativeStops++;}
'''+heartbeat+shutdown+'''}
class Check extends Base {boolean stopping;int stops;String lastMessage;void stopRelay(){stops++;stopping=true;}
'''+callback+'''public static void main(String[] args){
 Check s=new Check();s.onTaskRemoved(new Intent());assert s.stops==1&&s.stopping&&s.lastMessage.equals("Stopping relay…");s.onTaskRemoved(new Intent());assert s.stops==1&&s.callbacks==2;
 EngineCheck e=new EngineCheck();e.sendLinkState();assert e.packet.length==36&&e.packet[4]==Protocol.LINK&&ByteBuffer.wrap(e.packet).getInt(32)==2;
 e.stop();assert !e.running&&e.nativeStops==1&&e.socket.closed&&ByteBuffer.wrap(e.packet).getInt(32)==5;
 EngineCheck lost=new EngineCheck();lost.failSend=true;lost.stop();assert lost.nativeStops==1&&lost.socket.closed;
}}''')
 subprocess.run(['javac','-d',tmp,r/'android/app/src/main/java/com/aurora/relay/Protocol.java',r/'android/app/src/main/java/com/aurora/relay/SessionStatus.java',r/'android/app/src/main/java/com/aurora/relay/UsageProxy.java',r/'android/app/src/main/java/com/aurora/relay/UsageDelivery.java',p/'Check.java'],check=True);subprocess.run(['java','-ea','-cp',tmp,'com.aurora.relay.Check'],check=True)
print('PASS link packet validation; phone vs PS5/video states; 4s loss timeout/wrap/recovery; Android task close stops once')
