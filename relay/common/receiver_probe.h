#pragma once
/* Android-only socket diagnostics. No allocation, file IO or playback policy. */
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include "core_diag.h"
typedef struct { uint64_t last_video_us; uint32_t drops; } RelayRxState;
static inline uint64_t relay_rx_clock_us(clockid_t clock)
{
 struct timespec t; if(clock_gettime(clock,&t))return 0;
 return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static inline void relay_rx_setup(int fd,RelayRxState *state)
{
 memset(state,0,sizeof(*state));int on=1;
 if(!setsockopt(fd,SOL_SOCKET,SO_TIMESTAMPNS,&on,sizeof(on)))relay_rx_add(RRX_TIMESTAMP_ENABLED,1);
 if(!setsockopt(fd,SOL_SOCKET,SO_RXQ_OVFL,&on,sizeof(on)))relay_rx_add(RRX_OVERFLOW_ENABLED,1);
}
static inline ssize_t relay_rx_receive(int fd,uint8_t *buf,size_t cap,RelayRxState *state,unsigned mask,unsigned video_type)
{
 union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(struct timespec))+CMSG_SPACE(sizeof(uint32_t))]; } control;
 struct iovec iov={buf,cap};struct msghdr msg={0};msg.msg_iov=&iov;msg.msg_iovlen=1;msg.msg_control=control.bytes;msg.msg_controllen=sizeof(control.bytes);
 ssize_t n=recvmsg(fd,&msg,0);if(n<=0)return n;
 relay_rx_add(RRX_PACKETS,1);
 if(msg.msg_flags&MSG_TRUNC)relay_rx_add(RRX_TRUNCATED,1);
 if(msg.msg_flags&MSG_CTRUNC)relay_rx_add(RRX_CONTROL_TRUNCATED,1);
 uint64_t stamp=0;
 for(struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)){
  if(c->cmsg_level!=SOL_SOCKET)continue;
  if(c->cmsg_type==SO_TIMESTAMPNS&&c->cmsg_len>=CMSG_LEN(sizeof(struct timespec))){struct timespec t;memcpy(&t,CMSG_DATA(c),sizeof(t));stamp=(uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
  if(c->cmsg_type==SO_RXQ_OVFL&&c->cmsg_len>=CMSG_LEN(sizeof(uint32_t))){uint32_t drops;memcpy(&drops,CMSG_DATA(c),sizeof(drops));relay_rx_add(RRX_KERNEL_DROPS,(uint32_t)(drops-state->drops));state->drops=drops;}
 }
 if(stamp){
  relay_rx_add(RRX_STAMPED,1);uint64_t now=relay_rx_clock_us(CLOCK_REALTIME);
  if(now>=stamp){uint64_t age=now-stamp;relay_rx_max(RRX_QUEUE_MAX_US,age);if(age>=20000)relay_rx_add(RRX_QUEUE_20MS,1);}
  if((buf[0]&mask)==video_type){
   if(state->last_video_us&&stamp>=state->last_video_us){uint64_t gap=stamp-state->last_video_us;relay_rx_max(RRX_VIDEO_GAP_MAX_US,gap);if(gap>=75000)relay_rx_add(RRX_VIDEO_GAP_75MS,1);}
   state->last_video_us=stamp;
  }
 }
 return n;
}
