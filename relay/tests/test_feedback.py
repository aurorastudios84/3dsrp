#!/usr/bin/env python3
"""Regression tests against real receiver accounting code, including concurrent reset."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1];core=root.parent/'source/chiaki-ng'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 (p/'check.c').write_text(r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <chiaki/frameprocessor.h>
#include "core_diag.h"
static unsigned counts[RCD_COUNT];
void relay_core_count(unsigned k,unsigned n){counts[k]+=n;}
ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *m,bool recursive){return pthread_mutex_init(&m->mutex,NULL)?CHIAKI_ERR_UNKNOWN:CHIAKI_ERR_SUCCESS;}
ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *m){return pthread_mutex_lock(&m->mutex)?CHIAKI_ERR_UNKNOWN:CHIAKI_ERR_SUCCESS;}
ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *m){return pthread_mutex_unlock(&m->mutex)?CHIAKI_ERR_UNKNOWN:CHIAKI_ERR_SUCCESS;}
ChiakiErrorCode chiaki_mutex_fini(ChiakiMutex *m){return pthread_mutex_destroy(&m->mutex)?CHIAKI_ERR_UNKNOWN:CHIAKI_ERR_SUCCESS;}
static void expect(ChiakiPacketStats *s,unsigned r,unsigned l){uint64_t a,b;chiaki_packet_stats_get(s,true,&a,&b);assert(a==r&&b==l);}
static ChiakiPacketStats shared;static atomic_bool done;
static void *writer(void *unused){for(unsigned i=0;i<200000;i++)chiaki_packet_stats_push_seq(&shared,(uint16_t)i);atomic_store(&done,true);return NULL;}
int main(void){
 ChiakiPacketStats s;assert(!chiaki_packet_stats_init(&s));expect(&s,0,0);
 chiaki_packet_stats_push_seq(&s,65534);expect(&s,1,0); // arbitrary first sequence
 chiaki_packet_stats_push_seq(&s,65535);chiaki_packet_stats_push_seq(&s,0);expect(&s,2,0);
 chiaki_packet_stats_push_seq(&s,1);chiaki_packet_stats_push_seq(&s,1);expect(&s,2,0); // duplicate is not loss
 chiaki_packet_stats_push_seq(&s,3);expect(&s,1,1); // real loss still reported
 chiaki_packet_stats_push_seq(&s,5);chiaki_packet_stats_push_seq(&s,4);expect(&s,2,0);
 ChiakiFrameProcessor f;chiaki_frame_processor_init(&f,NULL);
 ChiakiTakionAVPacket packet={0};packet.units_in_frame_total=2;packet.data_size=8;
 assert(!chiaki_frame_processor_alloc_frame(&f,&packet));
 assert(f.units_fec_expected==1); // preserve FEC allocation and decoder behavior
 f.units_source_received=2;chiaki_frame_processor_report_packet_stats(&f,&s);expect(&s,2,0);
 f.units_source_received=1;chiaki_frame_processor_report_packet_stats(&f,&s);expect(&s,1,1);
 packet.units_in_frame_total=3;packet.units_in_frame_fec=1;
 assert(!chiaki_frame_processor_alloc_frame(&f,&packet));f.units_source_received=2;
 chiaki_frame_processor_report_packet_stats(&f,&s);expect(&s,2,1);
 f.units_fec_received=1;chiaki_frame_processor_report_packet_stats(&f,&s);expect(&s,3,0);
 assert(counts[RCD_ZERO_FEC_FRAMES]==1&&counts[RCD_PHANTOM_LOSS_AVOIDED]==2);
 chiaki_frame_processor_fini(&f);chiaki_packet_stats_fini(&s);
 chiaki_packet_stats_init(&shared);pthread_t t;pthread_create(&t,NULL,writer,NULL);
 uint64_t total=0,loss=0,a,b;
 while(!atomic_load(&done)){chiaki_packet_stats_get(&shared,true,&a,&b);total+=a;loss+=b;}
 pthread_join(t,NULL);chiaki_packet_stats_get(&shared,true,&a,&b);total+=a;loss+=b;
 assert(total==200000&&loss==0);chiaki_packet_stats_fini(&shared);
}
''')
 includes=[root/'common',core/'lib/include',root/'android/app/src/main/cpp/generated',core/'third-party/nanopb',core/'third-party/jerasure/include']
 # Locate the vendored Jerasure header used by the Android build.
 includes += [core/'third-party/gf-complete/include']
 cmd=['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DCHIAKI_ANDROID_RELAY','-DCHIAKI_LIB_ENABLE_LOG=0']
 cmd += ['-I'+str(x) for x in includes]
 cmd += [str(p/'check.c'),str(core/'lib/src/packetstats.c'),str(core/'lib/src/frameprocessor.c'),'-o',str(p/'check')]
 subprocess.run(cmd,check=True);subprocess.run([p/'check'],check=True)
print('PASS actual feedback accounting: no phantom FEC loss, real loss retained, startup/wrap/duplicate/reorder and concurrent resets')
