#pragma once
#include <stdbool.h>
#include "wire.h"
enum { RELAY_SOURCE_UNKNOWN=0,RELAY_SOURCE_WAITING=1,RELAY_SOURCE_ACTIVE=2,RELAY_SOURCE_TEST=3,RELAY_SOURCE_FAILED=4,RELAY_SOURCE_STOPPED=5 };
enum { RELAY_VIEW_WAIT_PHONE,RELAY_VIEW_PHONE_LOST,RELAY_VIEW_WAIT_PS5,RELAY_VIEW_WAIT_VIDEO,RELAY_VIEW_LIVE,RELAY_VIEW_VIDEO_STALLED,RELAY_VIEW_SOURCE_FAILED,RELAY_VIEW_VERSION,RELAY_VIEW_FORMAT };
static inline int relay_source_packet(const uint8_t *p,size_t n){
 if(!relay_valid(p,n)||p[4]!=RELAY_LINK||n!=RELAY_HEADER+4||r16(p+16)!=0||r16(p+18)!=1||r32(p+20)!=4)return -1;
 uint32_t state=r32(p+RELAY_HEADER);return state>=RELAY_SOURCE_WAITING&&state<=RELAY_SOURCE_STOPPED?(int)state:-1;
}
static inline unsigned relay_connection_view(bool phone,unsigned problem,unsigned source,bool fresh,bool had_video,bool recent_video){
 if(problem==1)return RELAY_VIEW_VERSION;
 if(problem==2)return RELAY_VIEW_FORMAT;
 if(!phone)return problem==3?RELAY_VIEW_PHONE_LOST:RELAY_VIEW_WAIT_PHONE;
 if(!fresh)return RELAY_VIEW_WAIT_PS5;
 if(source==RELAY_SOURCE_STOPPED)return RELAY_VIEW_PHONE_LOST;
 if(source==RELAY_SOURCE_FAILED)return RELAY_VIEW_SOURCE_FAILED;
 if(source!=RELAY_SOURCE_ACTIVE&&source!=RELAY_SOURCE_TEST)return RELAY_VIEW_WAIT_PS5;
 return recent_video?RELAY_VIEW_LIVE:had_video?RELAY_VIEW_VIDEO_STALLED:RELAY_VIEW_WAIT_VIDEO;
}
static inline const char *relay_view_title(unsigned view){
 switch(view){
 case RELAY_VIEW_WAIT_PHONE:return "Waiting for Android relay";
 case RELAY_VIEW_PHONE_LOST:return "Relay disconnected";
 case RELAY_VIEW_WAIT_PS5:return "Waiting for console";
 case RELAY_VIEW_WAIT_VIDEO:return "Waiting for video";
 case RELAY_VIEW_VIDEO_STALLED:return "Video interrupted";
 case RELAY_VIEW_SOURCE_FAILED:return "Console stream unavailable";
 case RELAY_VIEW_VERSION:return "App versions do not match";
 case RELAY_VIEW_FORMAT:return "Unsupported video format";
 default:return "Connected";
 }
}
