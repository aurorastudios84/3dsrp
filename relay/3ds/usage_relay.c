#include "usage_relay.h"
#include "wire.h"
#include "usage_platform.h"
#include <3ds.h>
#include <string.h>
#define PREFS "sdmc:/3ds/3dsrp-relay/usage.bin"
#define JOURNAL "sdmc:/3ds/3dsrp-relay/usage-pending.bin"
static UsageStats stats;
static UsagePreferences prefs;
static UsageRelaySend send_packet;
static uint32_t client_run,serial,last_frame,phone_session,request,last_checkpoint,last_retry;
static bool had_connection;
static LightLock ack_lock=1;
static int outcome;
static uint8_t payload[96];
static uint32_t next_serial(void){if(!++serial)++serial;return serial;}
static bool ok(void){return true;}
static bool body(const char *uuid,const UsageReport *r){
 memset(payload,0,sizeof(payload));w32(payload,client_run);memcpy(payload+4,uuid,36);memcpy(payload+40,r->session_id,36);memcpy(payload+76,r->day,10);
 payload[86]=1;w32(payload+88,r->milliseconds/1000u);w32(payload+92,prefs.generation);return true;
}
static bool begin(void){
 LightLock_Lock(&ack_lock);request=next_serial();outcome=0;uint32_t stamp=request;LightLock_Unlock(&ack_lock);
 send_packet(RELAY_USAGE_REPORT,payload,sizeof(payload),stamp);return true;
}
static int poll(void){LightLock_Lock(&ack_lock);int r=outcome;LightLock_Unlock(&ack_lock);return r;}
static void cancel(void){
 LightLock_Lock(&ack_lock);uint32_t stamp=outcome?0:request;request=0;outcome=0;LightLock_Unlock(&ack_lock);
 if(stamp){uint8_t p[4];w32(p,client_run);send_packet(RELAY_USAGE_CANCEL,p,sizeof(p),stamp);}
}
static void nothing(void){}
static const UsageHttpOps proxy={ok,ok,ok,ok,body,begin,poll,cancel,nothing,nothing};
void usage_relay_init(uint32_t run,UsageRelaySend send){
 client_run=run;serial=last_frame=phone_session=request=last_checkpoint=last_retry=0;outcome=0;had_connection=false;send_packet=send;usage_preferences_load(PREFS,&prefs);
 usage_stats_init(&stats,&proxy);usage_stats_set_journal(&stats,JOURNAL);
 // A damaged/unreadable preference file is never permission to send.
 usage_stats_enable(&stats,&prefs);
}
bool usage_relay_unasked(void){return prefs.choice==USAGE_UNASKED;}
bool usage_relay_allowed(void){return stats.enabled;}
bool usage_relay_choose(bool allow){
 UsagePreferences next=prefs;
 if(allow)usage_stats_hold(&stats,last_frame);
 else{UsagePreferences off=prefs;off.choice=USAGE_DECLINED;usage_stats_enable(&stats,&off);}
 if(!allow||prefs.choice!=USAGE_ALLOWED){if(next.generation==0xffffffu){prefs.choice=USAGE_DECLINED;return false;}next.generation++;}
 next.choice=allow?USAGE_ALLOWED:USAGE_DECLINED;
 if(allow&&!usage_uuid_valid(next.uuid)&&!usage_random_uuid(next.uuid)){prefs.choice=USAGE_DECLINED;return false;}
 if(!usage_preferences_save(PREFS,&next)){prefs.choice=USAGE_DECLINED;return false;}
 prefs=next;usage_stats_enable(&stats,&prefs);return true;
}
void usage_relay_receive(const uint8_t *p,size_t n){
 if(n!=RELAY_HEADER+8||p[4]!=RELAY_USAGE_ACK||r16(p+16)||r16(p+18)!=1||r32(p+20)!=8||r32(p+32)!=client_run)return;
 int result=(int32_t)r32(p+36);if(result!=1&&result!=2&&result!=-1)return;
 LightLock_Lock(&ack_lock);if(request&&request==r32(p+28))outcome=result;LightLock_Unlock(&ack_lock);
}
void usage_relay_suspend(void){if(stats.streaming)usage_stats_suspend(&stats,last_frame);}
void usage_relay_frame(uint32_t session,uint32_t now){
 if(phone_session!=session){if(phone_session){usage_stats_end_session(&stats,last_frame);usage_stats_begin_session(&stats);}phone_session=session;}
 bool first=!stats.session_id[0];usage_stats_frame(&stats,now);last_frame=now;
 // Crash recovery is local only. A checkpoint never makes current gameplay
 // eligible for HTTP delivery while video is running.
 if(stats.enabled&&stats.streaming&&(first||(uint32_t)(now-last_checkpoint)>=5000)){
  (void)usage_stats_checkpoint(&stats,now);last_checkpoint=now;
 }
}
void usage_relay_tick(bool connected,bool live,uint32_t now){
 if(!live)usage_relay_suspend();
 // Original 3DS sends only a small UDP record. Android does HTTPS off the
 // streaming threads, so new video must never cancel launch recovery.
 if(connected&&(!had_connection||(uint32_t)(now-last_retry)>=15000)){
  if(!stats.busy){usage_stats_retry_pending(&stats);last_retry=now;}
 }
 had_connection=connected;stats.defer_active=live&&stats.streaming;usage_stats_tick(&stats,connected,now);
}
void usage_relay_shutdown(bool connected){
 usage_stats_end_session(&stats,last_frame);usage_stats_shutdown(&stats);
 // Fire and retain: no exit delay. Only an acknowledged report removes completed SD records.
 // Replaying a lost final ACK on next launch is deduplicated by session/day at the server.
 if(connected&&stats.enabled){unsigned first=stats.count>8?stats.count-8:0;for(unsigned i=first;i<stats.count;i++){
  body(stats.uuid,&stats.reports[i]);send_packet(RELAY_USAGE_REPORT,payload,sizeof(payload),next_serial());
 }}
}
