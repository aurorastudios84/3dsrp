#!/usr/bin/env python3
"""Exercise the actual Android-only Takion AV delivery path and buffer ownership."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1];CORE=ROOT.parent/'source/chiaki-ng'
with tempfile.TemporaryDirectory(prefix='relay-android-ingress-') as tmp:
 p=Path(tmp)
 (p/'test.c').write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <chiaki/takion.h>
#include "core_diag.h"
static unsigned allocations,frees,delivered;static uint16_t order[32];static unsigned counts[RCD_COUNT];
void relay_rx_add(unsigned k,uint64_t n){}
void relay_core_count(unsigned k,unsigned n){assert(k<RCD_COUNT);counts[k]+=n;}
static void *tracked_malloc(size_t n){allocations++;return malloc(n);}
static void tracked_free(void *p){if(p)frees++;free(p);}
#define malloc tracked_malloc
#define free tracked_free
#include "TAKION_SOURCE"
#undef free
#undef malloc
static ChiakiErrorCode parse(ChiakiTakionAVPacket *p,ChiakiKeyState *key,uint8_t *b,size_t n){
 assert(n==8);memset(p,0,sizeof(*p));p->packet_index=(b[0]<<8)|b[1];p->frame_index=(b[2]<<8)|b[3];p->unit_index=b[4];p->is_video=b[6];p->data=b;p->data_size=n;return CHIAKI_ERR_SUCCESS;
}
static void callback(ChiakiTakionEvent *e,void *u){assert(e->type==CHIAKI_TAKION_EVENT_TYPE_AV);assert(delivered<32);order[delivered++]=e->av->packet_index;assert(e->av->data[7]==0xa5);}
static void push(ChiakiTakion *t,uint16_t index,uint16_t frame,unsigned unit,int video){
 uint8_t *b=malloc(8);assert(b);b[0]=index>>8;b[1]=index;b[2]=frame>>8;b[3]=frame;b[4]=unit;b[5]=0;b[6]=video;b[7]=0xa5;
 takion_handle_packet_av(t,video?TAKION_PACKET_TYPE_VIDEO:TAKION_PACKET_TYPE_AUDIO,b,8);
}
int main(void){
 ChiakiTakion t={0};t.cb=callback;t.av_packet_parse=parse;
 push(&t,65534,65535,0,1);push(&t,65535,65535,1,1);push(&t,0,0,0,1);
 push(&t,3,1,2,1);push(&t,2,1,1,1); /* no artificial wait for packet 1 */
 assert(delivered==5&&order[0]==65534&&order[2]==0&&order[3]==3&&order[4]==2);
 assert(!t.video_queue_initialized&&allocations==0&&frees==5);
 assert(counts[RCD_VIDEO_PACKETS]==5&&counts[RCD_VIDEO_FRAMES]==3&&counts[RCD_VIDEO_PACKET_GAPS]==2&&counts[RCD_VIDEO_FRAME_GAPS]==0);
 push(&t,12,0,0,0);assert(delivered==6&&frees==6);
 t.disable_audio_video=CHIAKI_VIDEO_DISABLED;push(&t,4,2,0,1);assert(delivered==6&&frees==7);
 t.disable_audio_video=CHIAKI_AUDIO_DISABLED;push(&t,13,0,0,0);assert(delivered==6&&frees==8);
}
'''.replace('TAKION_SOURCE',str(CORE/'lib/src/takion.c')))
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DCHIAKI_ANDROID_RELAY','-I'+str(ROOT/'common'),'-I'+str(CORE/'lib/include'),'-I'+str(ROOT/'android/app/src/main/cpp/generated'),'-I'+str(CORE/'third-party/nanopb'),p/'test.c','-o',p/'test'],check=True)
 subprocess.run([p/'test'],check=True)
print('PASS actual Android AV dispatch: immediate delivery across gaps/reordering/wrap, no custom queue/allocations, ownership and disable flags')
