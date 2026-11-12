#!/usr/bin/env python3
"""Signal-quality, cross-buffer continuity, packet capacity and pacing regression checks."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];src=root/'android/app/src/main/java/com/aurora/relay'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'AudioCheck.java').write_text('''package com.aurora.relay;
import java.nio.*;import java.io.*;import java.util.*;
public class AudioCheck {
 static ByteBuffer tone(double hz,int n){ByteBuffer b=ByteBuffer.allocate(n*4).order(ByteOrder.LITTLE_ENDIAN);for(int i=0;i<n;i++){short v=(short)(12000*Math.sin(2*Math.PI*hz*i/48000));b.putShort(v).putShort(v);}b.flip();return b;}
 static double rms(byte[] b){ByteBuffer x=ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN);double sum=0;int count=0;for(int i=0;x.remaining()>=2;i++){double v=x.getShort();if(i>100){sum+=v*v;count++;}}return Math.sqrt(sum/count);}
 public static void main(String[] args)throws Exception{
 byte[] low=new PcmResampler(48000,2).convert(tone(1000,48000)),high=new PcmResampler(48000,2).convert(tone(10000,48000));
 if(low.length!=24000||rms(low)<8000||rms(high)>rms(low)/100)throw new AssertionError("Filtering "+rms(low)+" / "+rms(high));
 ByteBuffer input=tone(1234,4800);byte[] all=new PcmResampler(48000,2).convert(input.duplicate().order(ByteOrder.LITTLE_ENDIAN));PcmResampler split=new PcmResampler(48000,2);ByteArrayOutputStream out=new ByteArrayOutputStream();
 while(input.hasRemaining()){int n=Math.min(input.remaining(),28);ByteBuffer part=input.slice().order(ByteOrder.LITTLE_ENDIAN);part.limit(n);out.write(split.convert(part));input.position(input.position()+n);}if(!Arrays.equals(all,out.toByteArray()))throw new AssertionError("Chunk discontinuity");
 PcmPacketQueue q=new PcmPacketQueue();byte[] a=new byte[480],b=new byte[480],c=new byte[480],d=new byte[480],got=new byte[480];Arrays.fill(a,(byte)1);Arrays.fill(b,(byte)2);Arrays.fill(c,(byte)3);Arrays.fill(d,(byte)4);
 q.offer(Arrays.copyOf(a,240));if(q.poll(0,got))throw new AssertionError();q.offer(Arrays.copyOf(a,240));if(q.poll(0,got))throw new AssertionError("Need startup headroom");q.offer(b);
 if(!q.poll(0,got)||!Arrays.equals(got,a)||!q.poll(0,got)||!Arrays.equals(got,b))throw new AssertionError("Startup packets");q.offer(c);if(q.poll(19999,got)||!q.poll(20000,got)||!Arrays.equals(got,c))throw new AssertionError("20ms pacing");
 q.offer(a);q.offer(b);q.offer(c);q.offer(d);if(!q.snapshot().equals("[1,3,3]"))throw new AssertionError(q.snapshot());if(!q.poll(100000,got)||!Arrays.equals(got,b)||q.poll(100000,got)||q.poll(109999,got)||!q.poll(110000,got))throw new AssertionError("Bounded backlog/catch-up");
 q.clear();q.offer(a);if(q.poll(200000,got))throw new AssertionError();q.offer(b);if(!q.poll(200000,got)||!Arrays.equals(got,a))throw new AssertionError("Reconnect priming");
 System.out.println("PASS audio: passband preserved, alias tone suppressed >40dB, chunk continuity, 40ms startup, 20ms packets, bounded queue and no catch-up burst");
 }}''')
 subprocess.run(['javac','-d',tmp,src/'PcmResampler.java',src/'PcmPacketQueue.java',p/'AudioCheck.java'],check=True)
 subprocess.run(['java','-cp',tmp,'com.aurora.relay.AudioCheck'],check=True)
