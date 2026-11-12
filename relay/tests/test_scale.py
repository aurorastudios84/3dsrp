#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='relay-scale-test-') as tmp:
 p=Path(tmp)
 (p/'test.c').write_text(r'''
#include "scale.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
int main(void){
 unsigned widths_out[]={240,320,400},heights_out[]={135,180,225};
 for(unsigned size=0;size<3;size++){unsigned width=widths_out[size],height=heights_out[size];
 for(unsigned test=0;test<120;test++){
  const uint8_t *planes[3];size_t caps[3];int layout[13]={0};
  int widths[]={640,1280,1920},heights[]={360,720,1080};int source_w=widths[test%3],source_h=heights[test%3];
  layout[9]=test%4;layout[10]=(test/4)%4;layout[11]=source_w-layout[9];layout[12]=source_h-layout[10];
  for(unsigned i=0;i<3;i++){
   int pixel=i?(1+test%3):1,row=(source_w/(i?2:1))*pixel+17;
   layout[i*3]=5;layout[i*3+1]=row;layout[i*3+2]=pixel;
   caps[i]=5+row*(source_h/(i?2:1));uint8_t *data=malloc(caps[i]);assert(data);
   for(size_t j=0;j<caps[i];j++)data[j]=(j*37+i*71+test)&255;planes[i]=data;
  }
  uint8_t output[relay_nv21_bytes(width,height)+32],reference[relay_nv21_bytes(width,height)];memset(output,0xcc,sizeof(output));
  assert(relay_scale_nv21(planes,caps,layout,output+16,sizeof(reference),width,height));
  for(unsigned i=0;i<3;i++){
   int div=i?2:1,w=i?width/2:width,h=i?(height+1)/2:height;
   for(int y=0;y<h;y++)for(int x=0;x<w;x++){
    double sum=0;double x0=(double)x*(layout[11]/div)/w,x1=(double)(x+1)*(layout[11]/div)/w;
    double y0=(double)y*(layout[12]/div)/h,y1=(double)(y+1)*(layout[12]/div)/h;
    for(int sy=(int)y0;sy<ceil(y1);sy++)for(int sx=(int)x0;sx<ceil(x1);sx++){
     double weight=(fmin(x1,sx+1)-fmax(x0,sx))*(fmin(y1,sy+1)-fmax(y0,sy));
     sum+=planes[i][layout[i*3]+(layout[10]/div+sy)*layout[i*3+1]+(layout[9]/div+sx)*layout[i*3+2]]*weight;
    }
    unsigned dest=i?width*height+y*width+x*2+(i==1?1:0):y*width+x;
    reference[dest]=(unsigned char)floor(sum/((x1-x0)*(y1-y0))+.5);

   }
  }
  for(unsigned k=0;k<sizeof(reference);k++)assert(abs(output[16+k]-reference[k])<=1);
  for(unsigned i=0;i<16;i++)assert(output[i]==0xcc&&output[sizeof(output)-1-i]==0xcc);
  assert(!relay_scale_nv21(planes,caps,layout,output+16,100,width,height));
  size_t bad[3]={1,caps[1],caps[2]};assert(!relay_scale_nv21(planes,bad,layout,output+16,sizeof(reference),width,height));
  for(unsigned i=0;i<3;i++)free((void*)planes[i]);
 }
}}
''')
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'common'),p/'test.c','-lm','-o',p/'test'],check=True)
 subprocess.run([p/'test'],check=True)
print('PASS native NV21 scaler matches independent floating-point area integration across 360 stride/crop/pixel layouts, output guards and invalid capacities')
