#!/usr/bin/env python3
"""Exercise the actual session calibration branch with controlled probe outcomes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root.parent/'source/chiaki-ng/lib/src/session.c').read_text()
start=s.index('#if defined(CHIAKI_OLD3DS_PROTOTYPE) && !defined(CHIAKI_ANDROID_RELAY)')
block=s[start:s.index('\tif(session->rudp)',start)]
body=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define ENABLE_SENKUSHA
#define CHIAKI_ERR_SUCCESS 0
#define CHIAKI_ERR_CANCELED 1
#define CHIAKI_LOGI(...) ((void)0)
#define CHIAKI_LOGE(...) ((void)0)
#define CORE_TRACE(...) ((void)0)
#define QUIT(x) goto x
#define CHECK_STOP(x) ((void)0)
typedef int ChiakiSenkusha;
typedef struct {unsigned mtu_in,mtu_out;uint64_t rtt_us;bool dontfrag,ctrl_failed;void *log;} Session;
static unsigned mode,runs,resets,finis,state,error;static uint64_t reported[4];
static uint64_t chiaki_time_now_monotonic_ms(void){return 123;}
static int chiaki_senkusha_init(ChiakiSenkusha *s,Session *session){return mode==3?9:0;}
static int chiaki_senkusha_run(ChiakiSenkusha *s,unsigned *in,unsigned *out,uint64_t *rtt,void *socket){runs++;*in=1200;*out=1300;*rtt=7000;return mode==1?8:mode==2?1:0;}
static void chiaki_senkusha_fini(ChiakiSenkusha *s){finis++;}
static void relay_calibration(unsigned st,unsigned err,uint64_t ms,unsigned in,unsigned out,uint64_t rtt){state=st;error=err;reported[0]=ms;reported[1]=in;reported[2]=out;reported[3]=rtt;}
static void relay_stream_metrics_reset(void){resets++;}
static int run(Session *session){int err=0;void *data_sock=0;
BLOCK
return 0;
quit_ctrl:return -1;
}
int main(void){
 Session s={0};assert(run(&s)==0);
#ifdef CHIAKI_ANDROID_RELAY
 assert(runs==1&&finis==1&&resets==1&&state==2&&error==0);
 assert(s.mtu_in==1200&&s.mtu_out==1300&&s.rtt_us==7000&&reported[3]==7000);
 mode=1;assert(run(&s)==0);assert(runs==2&&resets==2&&state==3&&error==8);
 assert(s.mtu_in==1454&&s.mtu_out==1454&&s.rtt_us==1000&&reported[1]==1454&&reported[3]==1000);
 mode=2;assert(run(&s)==-1&&state==4&&error==1&&resets==2&&finis==3);
 mode=3;assert(run(&s)==-1&&state==4&&error==9&&resets==2&&runs==3&&finis==3);
#else
 assert(runs==0&&resets==0&&s.mtu_in==1454&&s.mtu_out==1454&&s.rtt_us==1000);
#endif
}
'''.replace('BLOCK',block)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(body)
 for android in [False,True]:
  cmd=['cc','-O1','-fsanitize=address,undefined','-DCHIAKI_OLD3DS_PROTOTYPE']
  if android:cmd+=['-DCHIAKI_ANDROID_RELAY']
  subprocess.run(cmd+[str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([p/'test'],check=True)
print('PASS session calibration: Android measured values, fallback, cancel/init failure, stream-only reset; original 3DS bypass preserved')
