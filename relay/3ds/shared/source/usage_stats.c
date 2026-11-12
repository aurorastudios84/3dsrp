#include "usage_stats.h"
#include "usage_platform.h"
#include <string.h>
static void finish(UsageStats *s)
{
 if(s->context_open){s->http->cancel();s->http->close();}if(s->service_open)s->http->exit();s->busy=s->context_open=s->service_open=false;
}
static void transient_failure(UsageStats *s){memset(s->attempted,1,sizeof(s->attempted));finish(s);}
static void save(UsageStats *s){if(s->journal_path)(void)usage_journal_save(s->journal_path,s->uuid,s->generation,s->reports,s->count);}
void usage_stats_init(UsageStats *s,const UsageHttpOps *http){memset(s,0,sizeof(*s));s->http=http;}
void usage_stats_set_journal(UsageStats *s,const char *path){s->journal_path=path;}
static void remove_report(UsageStats *s,unsigned i)
{
 if(i>=s->count)return;
 memmove(s->reports+i,s->reports+i+1,(s->count-i-1)*sizeof(s->reports[0]));
 memmove(s->attempted+i,s->attempted+i+1,(s->count-i-1)*sizeof(s->attempted[0]));
 memmove(s->delivered+i,s->delivered+i+1,(s->count-i-1)*sizeof(s->delivered[0]));s->count--;
}
void usage_stats_enable(UsageStats *s,const UsagePreferences *prefs)
{
 finish(s);s->enabled=prefs->choice==USAGE_ALLOWED&&usage_uuid_valid(prefs->uuid);
 if(!s->enabled){s->streaming=false;s->session_id[0]=0;s->count=0;s->loaded=false;if(s->journal_path)(void)store_remove(s->journal_path);return;}
 if(strcmp(s->uuid,prefs->uuid)||s->generation!=prefs->generation){s->count=0;s->loaded=false;s->session_id[0]=0;s->streaming=false;}
 memcpy(s->uuid,prefs->uuid,sizeof(s->uuid));s->generation=prefs->generation;
 if(!s->loaded){s->count=0;if(s->journal_path)(void)usage_journal_load(s->journal_path,s->uuid,s->generation,s->reports,&s->count);s->loaded=true;memset(s->attempted,0,sizeof(s->attempted));memset(s->delivered,0,sizeof(s->delivered));}
}
static UsageReport *entry(UsageStats *s,const char *day)
{
 for(unsigned i=0;i<s->count;i++)if(!strcmp(s->reports[i].session_id,s->session_id)&&!strcmp(s->reports[i].day,day)){s->attempted[i]=s->delivered[i]=false;return &s->reports[i];}
 if(s->count==USAGE_QUEUE_CAPACITY)remove_report(s,0);
 UsageReport *r=&s->reports[s->count];*r=(UsageReport){0};memcpy(r->session_id,s->session_id,37);memcpy(r->day,day,11);s->delivered[s->count]=false;s->attempted[s->count++]=false;return r;
}
static void advance(UsageStats *s,uint32_t now)
{
 if(!s->enabled || !s->streaming)return;
 uint32_t left=now-s->stream_started;s->stream_started=now;
 if(left>USAGE_MAX_SECONDS*1000u)left=USAGE_MAX_SECONDS*1000u;
 do{
  char day[11];if(!usage_day_from_epoch((int64_t)(s->calendar_ms/1000u),day))return;
  UsageReport *r=entry(s,day);uint32_t available=USAGE_DAY_MS-(uint32_t)(s->calendar_ms%USAGE_DAY_MS),chunk=left<available?left:available;
  uint64_t total=(uint64_t)r->milliseconds+chunk;r->milliseconds=total>USAGE_DAY_MS?USAGE_DAY_MS:(uint32_t)total;
  s->calendar_ms+=chunk;left-=chunk;
 }while(left);
}
bool usage_stats_checkpoint(UsageStats *s,uint32_t now){advance(s,now);return !s->journal_path || usage_journal_save(s->journal_path,s->uuid,s->generation,s->reports,s->count);}
void usage_stats_suspend(UsageStats *s,uint32_t now){advance(s,now);s->streaming=false;finish(s);save(s);}
void usage_stats_hold(UsageStats *s,uint32_t now){usage_stats_suspend(s,now);s->enabled=false;}
void usage_stats_shutdown(UsageStats *s){finish(s);save(s);s->streaming=false;}
void usage_stats_begin_session(UsageStats *s){finish(s);s->streaming=false;s->session_id[0]=0;s->identity_failed=false;}
void usage_stats_frame(UsageStats *s,uint32_t now)
{
 if(!s->enabled || s->streaming || s->identity_failed)return;
 int64_t epoch=usage_platform_epoch();char day[11];if(!usage_day_from_epoch(epoch,day))return;
 if(!s->session_id[0]&&!usage_random_uuid(s->session_id)){s->identity_failed=true;return;}
 s->calendar_ms=(uint64_t)epoch*1000;s->stream_started=now;s->streaming=true;
}
void usage_stats_end_session(UsageStats *s,uint32_t now){if(!s->streaming&&!s->session_id[0])return;advance(s,now);s->streaming=false;s->session_id[0]=0;save(s);}
void usage_stats_retry_pending(UsageStats *s){for(unsigned i=0;i<s->count;i++)if(!s->delivered[i])s->attempted[i]=false;}
void usage_stats_tick(UsageStats *s,bool allowed,uint32_t now)
{
 if(!s->enabled || !allowed){if(s->busy)finish(s);return;}
 if(s->busy){
  if((uint32_t)(now-s->request_started)>=USAGE_TIMEOUT_MS){transient_failure(s);return;}
  int result=s->http->poll();if(!result)return;if(result<0){transient_failure(s);return;}UsageReport sent=s->in_flight_report;finish(s);
  // A checkpoint can update/reorder the queue while Android is sending an older snapshot.
  // Never delete newer cumulative time, and retain active-session baselines so the
  // next checkpoint sends a cumulative total rather than restarting at zero.
  for(unsigned index=0;index<s->count;index++){
   UsageReport *r=&s->reports[index];
   if(strcmp(r->session_id,sent.session_id)||strcmp(r->day,sent.day))continue;
   bool newer=r->milliseconds>sent.milliseconds,active=!strcmp(r->session_id,s->session_id);
   if(result==2 || (!newer&&!active))remove_report(s,index);
   else s->attempted[index]=s->delivered[index]=!newer;
   save(s);break;
  }return;
 }
 unsigned index=0;while(index<s->count&&(s->attempted[index] || (s->defer_active&&!strcmp(s->reports[index].session_id,s->session_id))))index++;
 if(index==s->count)return;
 s->attempted[index]=true;s->in_flight=index;s->in_flight_report=s->reports[index];s->request_started=now;s->busy=true;
 s->service_open=s->http->init();if(s->service_open)s->context_open=s->http->open();
 if(!s->context_open || !s->http->trust() || !s->http->headers() || !s->http->body(s->uuid,&s->reports[index]) || !s->http->begin_async())transient_failure(s);
}
