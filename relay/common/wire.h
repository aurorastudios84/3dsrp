#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "image_size.h"
#define RELAY_RELEASE 42u
#define RELAY_COMPAT_TAG 0x5233002au
#define RELAY_PORT 29870
#define RELAY_HEADER 32u
#define RELAY_CHUNK 1152u
#define RELAY_JPEG_MAX 98304u
#define RELAY_FRAG_MAX ((RELAY_JPEG_MAX+RELAY_CHUNK-1)/RELAY_CHUNK)
enum { RELAY_JPEG=1,RELAY_PCM=2,RELAY_HELLO=3,RELAY_INPUT=4,RELAY_PING=5,RELAY_PONG=6,RELAY_BYE=7,RELAY_INPUT_FULL=9,RELAY_CROP=10,RELAY_ERROR=11,RELAY_STATUS=12,RELAY_FEEDBACK=13,RELAY_LINK=14,RELAY_USAGE_REPORT=15,RELAY_USAGE_ACK=16,RELAY_USAGE_CANCEL=17 };
static inline uint16_t r16(const uint8_t *p){return (uint16_t)((p[0]<<8)|p[1]);}
static inline uint32_t r32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static inline void w16(uint8_t *p,uint16_t v){p[0]=v>>8;p[1]=v;}
static inline void w32(uint8_t *p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static inline int relay_valid(const uint8_t *p,size_t n){return n>=RELAY_HEADER&&n<=RELAY_HEADER+RELAY_CHUNK&&r32(p)==0x52335231&&r16(p+6)==RELAY_HEADER;}
static inline void relay_header(uint8_t *p,unsigned type,uint32_t session,uint32_t seq,unsigned index,unsigned count,unsigned total,unsigned w,unsigned h,uint32_t stamp){
 memset(p,0,RELAY_HEADER);w32(p,0x52335231);p[4]=type;w16(p+6,RELAY_HEADER);w32(p+8,session);w32(p+12,seq);w16(p+16,index);w16(p+18,count);w32(p+20,total);w16(p+24,w);w16(p+26,h);w32(p+28,stamp);
}
/* A receiver-thread heartbeat may be newer than the UI frame timestamp.
 * Signed modular age avoids treating that small negative age as 49 days. */
static inline int relay_link_expired(uint32_t now,uint32_t seen){return (int32_t)(now-seen)>4000;}
static inline int relay_hello_compatible(const uint8_t *p,size_t n){
 return relay_valid(p,n)&&n==RELAY_HEADER&&p[4]==RELAY_HELLO&&r32(p+12)==RELAY_COMPAT_TAG&&relay_image_size(r16(p+24),r16(p+26));
}
typedef struct {
 uint8_t data[RELAY_JPEG_MAX],seen[RELAY_FRAG_MAX];
 uint32_t seq,total;unsigned width,height,count,received,abandoned,missing_fragments,missing_sequences;int started,complete;
} RelayAssembly;
/* Independent JPEG pictures permit latest-sequence replacement, including
 * packet reordering and uint32 sequence wrap. Never drop H.264 this way. */
static inline int relay_fragment(RelayAssembly *a,const uint8_t *p,size_t n){
 if(!relay_valid(p,n)||p[4]!=RELAY_JPEG)return -1;
 uint32_t seq=r32(p+12),total=r32(p+20);unsigned index=r16(p+16),count=r16(p+18);
 if(!relay_image_size(r16(p+24),r16(p+26))||!total||total>RELAY_JPEG_MAX||count!=(total+RELAY_CHUNK-1)/RELAY_CHUNK||index>=count)return -1;
 unsigned offset=index*RELAY_CHUNK,need=total-offset;if(need>RELAY_CHUNK)need=RELAY_CHUNK;
 if(n!=RELAY_HEADER+need)return -1;
 if(a->started&&seq!=a->seq&&(uint32_t)(seq-a->seq)>=0x80000000u)return 0;
 if(!a->started||seq!=a->seq){
  if(a->started){unsigned delta=seq-a->seq;if(delta>1)a->missing_sequences+=delta-1;if(!a->complete){a->abandoned++;a->missing_fragments+=a->count-a->received;}}
  a->width=r16(p+24);a->height=r16(p+26);a->started=1;a->seq=seq;a->total=total;a->count=count;a->received=0;a->complete=0;memset(a->seen,0,sizeof(a->seen));}
 if(a->width!=r16(p+24)||a->height!=r16(p+26)||a->total!=total||a->count!=count)return -1;
 if(a->complete||a->seen[index])return 0;
 memcpy(a->data+offset,p+RELAY_HEADER,need);a->seen[index]=1;
 if(++a->received==count){a->complete=1;return 1;}return 0;
}
