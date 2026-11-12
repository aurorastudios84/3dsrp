#!/usr/bin/env python3
"""Complete Android AUs must reach MediaCodec unchanged; failed FEC must not."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1];CORE=ROOT.parent/'source/chiaki-ng'
source=CORE/'lib/src/videoreceiver.c'
with tempfile.TemporaryDirectory(prefix='relay-frame-delivery-') as temp:
 p=Path(temp)
 body=r'''
#include <assert.h>
#include <string.h>
#include "RECEIVER_SOURCE"
static unsigned callbacks,parse_calls,rewrite_calls,corrupts,counts[RCD_COUNT];
static bool test_allow_dispatch=true;static ChiakiFrameProcessorFlushResult result=CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_SUCCESS;
static uint8_t frame[]={0,0,0,1,0x41,0xa5,0x91,0x77};static const uint8_t original[]={0,0,0,1,0x41,0xa5,0x91,0x77};
void relay_core_count(unsigned key,unsigned n){assert(key<RCD_COUNT);counts[key]+=n;}
ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *m){return CHIAKI_ERR_SUCCESS;}
ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *m){return CHIAKI_ERR_SUCCESS;}
ChiakiFrameProcessorFlushResult chiaki_frame_processor_flush(ChiakiFrameProcessor *f,uint8_t **data,size_t *size){*data=frame;*size=sizeof(frame);return result;}
ChiakiErrorCode stream_connection_send_corrupt_frame(ChiakiStreamConnection *s,ChiakiSeqNum16 a,ChiakiSeqNum16 b){corrupts++;return CHIAKI_ERR_SUCCESS;}
ChiakiErrorCode stream_connection_send_idr_request(ChiakiStreamConnection *s){return CHIAKI_ERR_SUCCESS;}
void chiaki_session_send_event(ChiakiSession *s,ChiakiEvent *e){}
bool chiaki_bitstream_slice(ChiakiBitstream *b,uint8_t *data,unsigned n,ChiakiBitstreamSlice *slice){parse_calls++;slice->slice_type=CHIAKI_BITSTREAM_SLICE_P;slice->reference_frame=0;return true;}
bool chiaki_bitstream_slice_set_reference_frame(ChiakiBitstream *b,uint8_t *data,unsigned n,unsigned ref){rewrite_calls++;data[0]^=255;return true;}
static bool callback(uint8_t *data,size_t n,int32_t lost,bool recovered,void *user){callbacks++;assert(n==sizeof(original)&&!memcmp(data,original,n));return test_allow_dispatch;}
int main(void){
 ChiakiSession session={0};session.video_sample_cb=callback;
 ChiakiVideoReceiver v={0};v.session=&session;v.frame_index_cur=7;v.frame_index_prev_complete=5;v.frames_lost=3;
 for(unsigned i=0;i<16;i++)v.reference_frames[i]=-1;
 v.reference_frames[0]=5; /* heuristic would incorrectly rewrite to this */
 assert(chiaki_video_receiver_flush_frame(&v)==CHIAKI_ERR_SUCCESS);
 assert(callbacks==1&&!parse_calls&&!rewrite_calls&&v.frames_lost==0&&v.frame_index_prev_complete==7);
 assert(counts[RCD_VIDEO_CALLBACKS]==1&&!counts[RCD_REFERENCE_REJECTED]);
 test_allow_dispatch=false;v.frame_index_cur=8;assert(chiaki_video_receiver_flush_frame(&v)==CHIAKI_ERR_SUCCESS);
 assert(callbacks==2&&v.frame_index_prev==8&&v.frame_index_prev_complete==7&&counts[RCD_CALLBACK_REJECTED]==1);
 test_allow_dispatch=true;v.frame_index_cur=9;result=CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED;
 assert(chiaki_video_receiver_flush_frame(&v)==CHIAKI_ERR_UNKNOWN&&callbacks==2);
 result=CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED;
 assert(chiaki_video_receiver_flush_frame(&v)==CHIAKI_ERR_UNKNOWN&&callbacks==2&&corrupts==1);
 result=CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_SUCCESS;v.frame_index_cur=10;
 assert(chiaki_video_receiver_flush_frame(&v)==CHIAKI_ERR_SUCCESS&&callbacks==3&&v.frame_index_prev_complete==10);
 assert(!parse_calls&&!rewrite_calls&&!memcmp(frame,original,sizeof(frame)));
}
'''
 def compile_and_run(path,name):
  c=p/(name+'.c');c.write_text(body.replace('RECEIVER_SOURCE',str(path)));exe=p/name
  subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DCHIAKI_ANDROID_RELAY','-I'+str(ROOT/'common'),'-I'+str(CORE/'lib/include'),'-I'+str(ROOT/'android/app/src/main/cpp/generated'),'-I'+str(CORE/'third-party/nanopb'),c,'-o',exe],check=True)
  return subprocess.run([exe],capture_output=True)
 assert compile_and_run(source,'test').returncode==0
 mutant=source.read_text().replace('#ifndef CHIAKI_ANDROID_RELAY\n\tbool slice_parsed=', '#if 1\n\tbool slice_parsed=',1)
 assert mutant!=source.read_text()
 mutant=mutant.replace('"../include/chiaki/session.h"','"'+str(CORE/'lib/include/chiaki/session.h')+'"')
 (p/'mutant.c').write_text(mutant)
 assert compile_and_run(p/'mutant.c','old_policy').returncode!=0
print('PASS actual Android complete-frame delivery: unchanged bytes, no guessed-reference rejection/rewrite, callback refusal and failed FEC preserved; old-policy mutation rejected')
