#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {const uint8_t *p,*end;unsigned byte,bits,zeros;} RelayRbsp;
static inline int relay_rbsp_bit(RelayRbsp *r,unsigned *out){
 if(!r->bits){if(r->p==r->end)return 0;unsigned b=*r->p++;
  if(r->zeros>=2&&b==1)return 0; /* next Annex B NAL, not header data */
  if(r->zeros>=2&&b==3){if(r->p==r->end)return 0;b=*r->p++;r->zeros=0;}
  r->zeros=b==0?r->zeros+1:0;r->byte=b;r->bits=8;
 }
 *out=(r->byte>>(--r->bits))&1;return 1;
}
static inline int relay_rbsp_bits(RelayRbsp *r,unsigned bits,unsigned *out){unsigned v=0,b;for(unsigned i=0;i<bits;i++){if(!relay_rbsp_bit(r,&b))return 0;v=(v<<1)|b;}*out=v;return 1;}
static inline int relay_rbsp_ue(RelayRbsp *r,unsigned *out){unsigned zeros=0,b,tail=0;while(1){if(!relay_rbsp_bit(r,&b))return 0;if(b)break;if(++zeros>30)return 0;}if(!relay_rbsp_bits(r,zeros,&tail))return 0;*out=((1u<<zeros)-1)+tail;return 1;}
/* Read-only diagnostics. H.264 frame_num is distinct from the transport's
 * frame_index. This observation never accepts/rejects/rewrites a picture. */
static inline int relay_h264_frame_number(const uint8_t *data,size_t bytes,unsigned bits,unsigned *number,unsigned *idr,unsigned *reference){
 if(!data||bits<4||bits>16)return 0;
 for(size_t i=0;i+3<bytes;i++){
  if(data[i]!=0||data[i+1]!=0)continue;size_t nal;
  if(data[i+2]==1)nal=i+3;else if(i+4<bytes&&data[i+2]==0&&data[i+3]==1)nal=i+4;else continue;
  unsigned type=data[nal]&31;if(type!=1&&type!=5)continue;
  RelayRbsp r={.p=data+nal+1,.end=data+bytes};unsigned ignored;
  if(!relay_rbsp_ue(&r,&ignored)||!relay_rbsp_ue(&r,&ignored)||!relay_rbsp_ue(&r,&ignored)||!relay_rbsp_bits(&r,bits,number))return 0;
  *idr=type==5;*reference=(data[nal]&0x60)!=0;return 1;
 }
 return 0;
}
