#pragma once
#include <stdint.h>
#include <stddef.h>
#include "image_size.h"
#define RELAY_WIDTH 400
#define RELAY_HEIGHT 225
#define RELAY_NV21_BYTES (400*225+400*113)
/* Area average: integrate every contributing source pixel, including fractional
 * edges. Odd output height retains a final chroma row; native JPEG accepts it. */
static inline int relay_scale_nv21(const uint8_t *planes[3],const size_t capacity[3],const int layout[13],uint8_t *out,size_t out_bytes,unsigned width,unsigned height){
 if(!relay_image_size(width,height)||!out||out_bytes<relay_nv21_bytes(width,height)||layout[9]<0||layout[10]<0||layout[11]<2||layout[12]<2||layout[11]>8192||layout[12]>8192)return 0;
 for(unsigned p=0;p<3;p++){
  int base=layout[p*3],row=layout[p*3+1],pixel=layout[p*3+2];unsigned div=p?2:1,w=p?width/2:width,h=p?(height+1)/2:height;
  if(!planes[p]||base<0||row<=0||pixel<=0)return 0;
  unsigned left=(unsigned)layout[9]/div,top=(unsigned)layout[10]/div,sw=(unsigned)layout[11]/div,sh=(unsigned)layout[12]/div;
  uint64_t maximum=(uint64_t)base+((uint64_t)top+sh-1)*(unsigned)row+((uint64_t)left+sw-1)*(unsigned)pixel;
  if(maximum>=capacity[p])return 0;
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
   unsigned xa=x*sw,xb=(x+1)*sw,ya=y*sh,yb=(y+1)*sh;uint64_t sum=0;
   for(unsigned sy=ya/h;sy<(yb+h-1)/h;sy++){
    unsigned wy=(yb<(sy+1)*h?yb:(sy+1)*h)-(ya>sy*h?ya:sy*h);
    const uint8_t *src=planes[p]+base+(size_t)(top+sy)*row;
    for(unsigned sx=xa/w;sx<(xb+w-1)/w;sx++){
     unsigned wx=(xb<(sx+1)*w?xb:(sx+1)*w)-(xa>sx*w?xa:sx*w);
     sum+=(uint64_t)src[(size_t)(left+sx)*pixel]*wx*wy;
    }
   }
   unsigned dest=p?width*height+y*width+x*2+(p==1?1:0):y*width+x;
   out[dest]=(uint8_t)((sum+(uint64_t)sw*sh/2)/((uint64_t)sw*sh));
  }
 }
 return 1;
}
