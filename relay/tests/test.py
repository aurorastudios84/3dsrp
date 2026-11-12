#!/usr/bin/env python3
from pathlib import Path
from PIL import Image
import subprocess,tempfile,json,random
ROOT=Path(__file__).resolve().parents[1]
def run(args,**kw):return subprocess.run(list(map(str,args)),check=True,**kw)
with tempfile.TemporaryDirectory(prefix='relay-test-') as temp:
 t=Path(temp);image=Image.new('RGB',(400,225),(240,15,10));image.save(t/'picture.jpg',quality=30)
 for width,height in [(240,135),(320,180)]:Image.new('RGB',(width,height),(240,15,10)).save(t/f'{width}.jpg',quality=30)
 bad=Image.new('RGB',(640,360));bad.save(t/'wrong.jpg',quality=30)
 source=(ROOT/'3ds/main.c').read_text();jpeg=source[source.index('struct JpegError {'):source.index('static unsigned spread(')]
 prefix='''#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <setjmp.h>
#include <assert.h>
#include <jpeglib.h>
#include "wire.h"
static uint16_t rgb[400*225];
'''
 body=r'''
static uint8_t packet[RELAY_HEADER+RELAY_CHUNK];
static void make(unsigned seq,unsigned index,unsigned count,unsigned total,const uint8_t *data) {
 unsigned n=total-index*RELAY_CHUNK;if(n>RELAY_CHUNK)n=RELAY_CHUNK;
 relay_header(packet,RELAY_JPEG,1,seq,index,count,total,400,225,0);
 memcpy(packet+RELAY_HEADER,data+index*RELAY_CHUNK,n);
}
int main(int argc,char **argv) {
 uint8_t data[RELAY_JPEG_MAX];for(unsigned i=0;i<sizeof(data);i++)data[i]=(i*37+3)&255;
 RelayAssembly *a=calloc(1,sizeof(*a));assert(a);
 unsigned total=3*RELAY_CHUNK+19;
 for(unsigned seq=1;seq<=1000;seq++) {
  unsigned order[]={2,0,3,1};
  for(unsigned i=0;i<4;i++) {
   unsigned index=order[i],n=index==3?19:RELAY_CHUNK;make(seq,index,4,total,data);
   assert(relay_fragment(a,packet,RELAY_HEADER+n)==(i==3?1:0));
   assert(relay_fragment(a,packet,RELAY_HEADER+n)==0); /* duplicates */
  }
  assert(a->complete&&a->total==total&&!memcmp(a->data,data,total));
  make(seq-1,0,4,total,data);assert(relay_fragment(a,packet,RELAY_HEADER+RELAY_CHUNK)==0);
 }
 /* Loss discards an obsolete partial frame; next independent JPEG completes. */
 make(1001,0,4,total,data);assert(relay_fragment(a,packet,RELAY_HEADER+RELAY_CHUNK)==0);
 make(1002,0,1,19,data);assert(relay_fragment(a,packet,RELAY_HEADER+19)==1);
 assert(!memcmp(a->data,data,19));assert(a->abandoned==1&&a->missing_fragments==3);
 /* All sizes assemble; mixed dimensions within a frame are rejected. */
 unsigned ws[]={240,320,400},hs[]={135,180,225};
 for(unsigned i=0;i<3;i++){
  memset(a,0,sizeof(*a));make(1,0,4,total,data);w16(packet+24,ws[i]);w16(packet+26,hs[i]);assert(relay_fragment(a,packet,RELAY_HEADER+RELAY_CHUNK)==0);
  make(1,1,4,total,data);w16(packet+24,ws[(i+1)%3]);w16(packet+26,hs[(i+1)%3]);assert(relay_fragment(a,packet,RELAY_HEADER+RELAY_CHUNK)==-1);
  for(unsigned j=1;j<4;j++){make(1,j,4,total,data);w16(packet+24,ws[i]);w16(packet+26,hs[i]);assert(relay_fragment(a,packet,RELAY_HEADER+(j==3?19:RELAY_CHUNK))==(j==3?1:0));}
 }
 /* Explicit wrap and malformed-size rejection. */
 memset(a,0,sizeof(*a));make(0xffffffffu,0,1,19,data);assert(relay_fragment(a,packet,RELAY_HEADER+19)==1);
 make(0,0,1,19,data);assert(relay_fragment(a,packet,RELAY_HEADER+19)==1);
 w32(packet+20,RELAY_JPEG_MAX+1);assert(relay_fragment(a,packet,RELAY_HEADER+19)==-1);
 for(unsigned i=0;i<RELAY_HEADER;i++)assert(relay_fragment(a,packet,i)==-1);
 FILE *f=fopen(argv[1],"rb");assert(f);size_t bytes=fread(data,1,sizeof(data),f);fclose(f);
 assert(decode_picture(data,bytes,400,225));assert((rgb[100*400+100]>>11)>25);assert(((rgb[100*400+100]>>5)&63)<10);
 assert(!decode_picture(data,16,400,225));memset(data,0,100);assert(!decode_picture(data,100,400,225));
 f=fopen(argv[2],"rb");assert(f);bytes=fread(data,1,sizeof(data),f);fclose(f);assert(!decode_picture(data,bytes,400,225));
 for(unsigned i=0;i<2;i++){
  f=fopen(argv[3+i],"rb");assert(f);bytes=fread(data,1,sizeof(data),f);fclose(f);
  unsigned w=i?320:240,h=i?180:135;assert(decode_picture(data,bytes,w,h));assert((rgb[100*w+100]>>11)>25);
  assert(!decode_picture(data,bytes,400,225));
 }
 free(a);puts("PASS actual JPEG decode/RGB565, bad JPEG/dimensions, packet loss/reorder/duplicates/wrap/bounds (1000 frames)");
}
'''
 (t/'test.c').write_text(prefix+jpeg+body)
 run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'common'),'-I'+str(ROOT/'output/host-jpeg'),'-I'+str(ROOT/'vendor/libjpeg-turbo-3.1.3/src'),t/'test.c',ROOT/'output/host-jpeg/libjpeg.a','-lm','-o',t/'test'])
 run([t/'test',t/'picture.jpg',t/'wrong.jpg',t/'240.jpg',t/'320.jpg'])
 # Actual Java header serializer interoperation with the C receiver's endian layout.
 (t/'ProtocolTest.java').write_text('''package com.aurora.relay;
import java.nio.file.*;
public class ProtocolTest { public static void main(String[] args)throws Exception {
 byte[] data=new byte[]{1,2,3};byte[] p=Protocol.packet(1,0x12345678,0xfffffff0,0,1,3,400,225,123,data,0,3);
 if(!Protocol.valid(p,p.length))throw new AssertionError();Files.write(Paths.get(args[0]),p);
}}
''')
 run(['javac','-d',t,ROOT/'android/app/src/main/java/com/aurora/relay/Protocol.java',t/'ProtocolTest.java'])
 run(['java','-cp',t,'com.aurora.relay.ProtocolTest',t/'java-packet.bin'])
 import struct
 p=(t/'java-packet.bin').read_bytes()
 assert struct.unpack('>4sBBHIIHHIHHI',p[:32])==(b'R3R1',1,0,32,0x12345678,0xfffffff0,0,1,3,400,225,123)
 assert p[32:]==b'\x01\x02\x03'
 print('PASS actual Android Java packet serialization and C wire layout')
(ROOT/'output/host-tests.json').write_text(json.dumps({'status':'passed','jpeg_decode_rgb565':True,'malformed_jpeg_and_dimensions':True,'reassembly_frames':1000,'loss_reorder_duplicate_wrap_bounds':True,'java_c_wire_interoperation':True,'sanitizers':'host receiver source; dependency built generic C'},indent=2)+'\n')
