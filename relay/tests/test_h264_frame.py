#!/usr/bin/env python3
from pathlib import Path
import random,subprocess,tempfile,struct
ROOT=Path(__file__).resolve().parents[1];rng=random.Random(7911)
def ue(n):s=bin(n+1)[2:];return '0'*(len(s)-1)+s
with tempfile.TemporaryDirectory(prefix='relay-frame-numbers-') as temp:
 p=Path(temp);rows=[]
 for i in range(500):
  bits=rng.randrange(4,17);number=rng.randrange(1<<bits);idr=i%7==0;ref=i%5!=0
  s=ue(rng.randrange(100000))+ue(2 if idr else 0)+ue(rng.randrange(256))+format(number,'0%db'%bits)+'1';s+='0'*((-len(s))%8)
  raw=bytes(int(s[j:j+8],2) for j in range(0,len(s),8));encoded=bytearray();zeros=0
  for b in raw:
   if zeros>=2 and b<=3:encoded.append(3);zeros=0
   encoded.append(b);zeros=zeros+1 if b==0 else 0
  data=b'\0\0\0\1'+bytes([(0x60 if ref else 0)|(5 if idr else 1)])+encoded
  rows.append('{%d,%d,%d,%d,%d,{%s}}'%(len(data),bits,number,idr,ref,','.join(str(x) for x in data)))
 (p/'test.c').write_text('''#include "h264_frame.h"
#include <assert.h>
#include <string.h>
struct Sample {unsigned size,bits,number,idr,reference;uint8_t data[64];};
static struct Sample cases[]={'''+','.join(rows)+'''};
int main(void){
 for(unsigned i=0;i<500;i++){struct Sample *s=&cases[i];uint8_t saved[64];memcpy(saved,s->data,64);unsigned n,k,r;
 assert(relay_h264_frame_number(s->data,s->size,s->bits,&n,&k,&r));assert(n==s->number&&k==s->idr&&r==s->reference);assert(!memcmp(saved,s->data,64));
 assert(!relay_h264_frame_number(s->data,5,s->bits,&n,&k,&r));assert(!relay_h264_frame_number(s->data,s->size,32,&n,&k,&r));
 }return 0;}
''')
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'common'),p/'test.c','-o',p/'test'],check=True)
 subprocess.run([p/'test'],check=True)
# Also check real x264 access units and the existing SPS frame-width reader.
with tempfile.TemporaryDirectory(prefix='relay-real-frame-numbers-') as temp:
 p=Path(temp);core=ROOT.parent/'source/chiaki-ng'
 (p/'test.c').write_text(r'''
#include "h264_frame.h"
#include <chiaki/bitstream.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned read32(FILE *f){unsigned char b[4];assert(fread(b,1,4,f)==4);return ((unsigned)b[0]<<24)|((unsigned)b[1]<<16)|(b[2]<<8)|b[3];}
int main(int argc,char **argv){FILE *f=fopen(argv[1],"rb");assert(f);unsigned count=read32(f);assert(count==30);ChiakiBitstream stream;chiaki_bitstream_init(&stream,NULL,CHIAKI_CODEC_H264);
 for(unsigned i=0;i<count;i++){
  unsigned size=read32(f);uint8_t *data=malloc(size),*saved=malloc(size);assert(data&&saved&&fread(data,1,size,f)==size);memcpy(saved,data,size);
  if(!i)assert(chiaki_bitstream_header(&stream,data,size));
  unsigned bits=stream.h264.sps.log2_max_frame_num_minus4+4,n,key,ref;assert(relay_h264_frame_number(data,size,bits,&n,&key,&ref));
  assert(n==(i&((1u<<bits)-1))&&key==(i==0)&&ref);assert(!memcmp(data,saved,size));free(data);free(saved);
 }
 fclose(f);
 unsigned n,k,r;uint8_t cut[]={0,0,1,0x41,0,0,1,0x65,0xff};assert(!relay_h264_frame_number(cut,sizeof(cut),4,&n,&k,&r));
}
''')
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'common'),'-I'+str(core/'lib/include'),'-I'+str(ROOT/'android/app/src/main/cpp/generated'),p/'test.c',core/'lib/src/bitstream.c','-o',p/'test'],check=True)
 subprocess.run([p/'test',ROOT/'android/app/src/main/assets/fixture-video.bin'],check=True)
print('PASS actual 30-picture x264 fixture frame numbering, SPS bit width, IDR flags and truncated-NAL boundaries')
print('PASS read-only H.264 frame_num observer: 500 bounded RBSP/emulation-prevention cases, frame widths, IDR/ref flags, malformed input and immutable bytes')
