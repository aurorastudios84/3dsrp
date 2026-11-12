package com.aurora.relay;
import java.nio.*;
final class Protocol {
 static final int RELEASE=42,COMPAT=0x5233002a;
 static boolean compatibleHello(byte[] p,int n){return valid(p,n)&&n==HEADER&&(p[4]&255)==HELLO&&ByteBuffer.wrap(p).getInt(12)==COMPAT;}
 static final int PORT=29870,HEADER=32,CHUNK=1152,MAX_JPEG=98304;
 static final int JPEG=1,PCM=2,HELLO=3,INPUT=4,PING=5,PONG=6,BYE=7,INPUT_FULL=9,ERROR=11,STATUS=12,FEEDBACK=13,LINK=14,USAGE_REPORT=15,USAGE_ACK=16,USAGE_CANCEL=17;
 static byte[] packet(int type,int session,int seq,int fragment,int fragments,int total,int w,int h,int timestamp,byte[] payload,int offset,int size){
  if(size<0||size>CHUNK||offset<0||(size>0&&(payload==null||offset+size>payload.length)))throw new IllegalArgumentException();
  ByteBuffer b=ByteBuffer.allocate(HEADER+size).order(ByteOrder.BIG_ENDIAN);
  b.putInt(0x52335231).put((byte)type).put((byte)0).putShort((short)HEADER);
  b.putInt(session).putInt(seq).putShort((short)fragment).putShort((short)fragments).putInt(total);
  b.putShort((short)w).putShort((short)h).putInt(timestamp);
  if(size>0)b.put(payload,offset,size);return b.array();
 }
 static boolean valid(byte[] b,int n){return n>=HEADER&&n<=HEADER+CHUNK&&ByteBuffer.wrap(b).getInt()==0x52335231&&(b[6]&255)==0&&(b[7]&255)==HEADER;}
}
