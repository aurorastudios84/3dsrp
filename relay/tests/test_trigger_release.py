#!/usr/bin/env python3
"""Actual threaded feedback sender: release loss, long bursts, ordering and idle retries."""
from pathlib import Path
import subprocess,tempfile,json,argparse,shutil,resource
args=argparse.ArgumentParser(description=__doc__);args.add_argument("--baseline",type=Path);args=args.parse_args()
r=Path(__file__).resolve().parents[1];core=r/'vendor/chiaki-upstream';app=r/'3ds/shared'
source=r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <chiaki/feedbacksender.h>
#include <chiaki/time.h>
#include "controls.h"
#include "controller_wire.h"
static atomic_uint histories,states,remote_value,dropped,budget,target_code,bad_releases;
static atomic_bool must_stay_held;
static void pause_ms(unsigned n){struct timespec t={n/1000,(n%1000)*1000000L};nanosleep(&t,NULL);}
ChiakiErrorCode chiaki_takion_send_feedback_state(ChiakiTakion *t,ChiakiSeqNum16 seq,ChiakiFeedbackState *s){atomic_fetch_add(&states,1);return CHIAKI_ERR_SUCCESS;}
ChiakiErrorCode chiaki_takion_send_feedback_history(ChiakiTakion *t,ChiakiSeqNum16 seq,uint8_t *p,size_t n){
 /* All controls used in this fixture have three-byte absolute events.
  * Decode newest first, just once per control, to model its received value. */
 assert(n&&n%3==0);bool found=false;unsigned value=0;
 for(size_t i=0;i<n;i+=3){assert(p[i]==0x80);if(!found&&p[i+1]==atomic_load(&target_code)){value=p[i+2];found=true;}}
 bool drop=found&&value==0&&atomic_load(&budget)>0;
 if(drop){atomic_fetch_sub(&budget,1);atomic_fetch_add(&dropped,1);}
 else if(found){atomic_store(&remote_value,value);if(value==0&&atomic_load(&must_stay_held))atomic_fetch_add(&bad_releases,1);}
 atomic_fetch_add(&histories,1);return CHIAKI_ERR_SUCCESS;
}
static uint64_t now(void){return chiaki_time_now_monotonic_ms();}
static void wait_value(unsigned value){uint64_t end=now()+2500;while(atomic_load(&remote_value)!=value&&now()<end)pause_ms(2);assert(atomic_load(&remote_value)==value);}
static void submit(ChiakiFeedbackSender *f,RelayControls *u,unsigned held,int steering){
 Input3dsSnapshot input={0};input.held=held;input.circle_x=steering;
 ChiakiControllerState a,b;relay_controls_map(u,&input,&a);
 assert(a.l2_state==((held&RP_INPUT_L)?255:0));assert(a.r2_state==((held&RP_INPUT_R)?255:0));
 uint8_t wire[RELAY_CONTROL_BYTES];relay_control_encode(wire,&a);assert(relay_control_decode(wire,sizeof(wire),&b));
 assert(b.l2_state==a.l2_state&&b.r2_state==a.r2_state);
 assert(chiaki_feedback_sender_set_controller_state(f,&b)==CHIAKI_ERR_SUCCESS);
}
static void reset(unsigned code){atomic_store(&histories,0);atomic_store(&states,0);atomic_store(&remote_value,0);atomic_store(&dropped,0);atomic_store(&budget,0);atomic_store(&target_code,code);atomic_store(&bad_releases,0);atomic_store(&must_stay_held,false);}
static void loss_case(unsigned key,unsigned code,unsigned losses,bool steer){
 reset(code);ChiakiTakion takion={0};ChiakiFeedbackSender f;RelayControls u={0};input_3ds_reset_capture(&u.touch);
 assert(chiaki_feedback_sender_init(&f,&takion)==CHIAKI_ERR_SUCCESS);
 pause_ms(30);assert(atomic_load(&histories)==0); // no uninitialized retry packet
 submit(&f,&u,key,0);wait_value(255);
 atomic_store(&budget,losses);uint64_t start=now();submit(&f,&u,0,0);
 unsigned updates=0;while((atomic_load(&remote_value)!=0||atomic_load(&budget))&&now()-start<2500){submit(&f,&u,0,steer?((updates&1)?156:-156):0);updates++;pause_ms(5);}
 assert(atomic_load(&remote_value)==0&&atomic_load(&dropped)==losses);
 uint64_t recovered=now()-start;assert(recovered<1800);
 unsigned sent=atomic_load(&histories);pause_ms(450);
 assert(atomic_load(&remote_value)==0); // released remains released
 #ifndef EXPECT_ORIGINAL
 assert(atomic_load(&histories)>sent&&atomic_load(&histories)<30); // ongoing, bounded refresh
 #endif
 uint64_t stop=now();chiaki_feedback_sender_fini(&f);assert(now()-stop<100);
 printf("{\"key\":%u,\"lost_release_packets\":%u,\"steering\":%s,\"recovered_ms\":%llu,\"history_packets\":%u}",key,losses,steer?"true":"false",(unsigned long long)recovered,atomic_load(&histories));
}
static void supersede_case(void){
 reset(0x86);ChiakiTakion takion={0};ChiakiFeedbackSender f;RelayControls u={0};input_3ds_reset_capture(&u.touch);
 assert(chiaki_feedback_sender_init(&f,&takion)==CHIAKI_ERR_SUCCESS);
 submit(&f,&u,RP_INPUT_L,0);wait_value(255);atomic_store(&budget,1);submit(&f,&u,0,0);
 uint64_t end=now()+1000;while(!atomic_load(&dropped)&&now()<end)pause_ms(1);assert(atomic_load(&dropped)==1);
 submit(&f,&u,RP_INPUT_L,0);unsigned before=atomic_load(&histories);end=now()+1000;
 while(atomic_load(&histories)==before&&now()<end)pause_ms(1);
 atomic_store(&must_stay_held,true);pause_ms(450);assert(atomic_load(&remote_value)==255&&atomic_load(&bad_releases)==0);
 atomic_store(&must_stay_held,false);submit(&f,&u,0,0);wait_value(0);
 chiaki_feedback_sender_fini(&f);
}
int main(void){
 printf("{\"loss_cases\":[");loss_case(RP_INPUT_L,0x86,0,true);printf(",");loss_case(RP_INPUT_L,0x86,1,true);printf(",");loss_case(RP_INPUT_L,0x86,4,false);printf(",");loss_case(RP_INPUT_L,0x86,8,true);printf(",");loss_case(RP_INPUT_R,0x87,1,false);printf(",");loss_case(RP_INPUT_A,0x89,1,true);
 supersede_case();puts("],\"newer_press_supersedes_old_release\":true,\"idle_refresh_bounded\":true,\"clean_shutdown\":true}");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.c').write_text(source)
 cmd=['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
 cmd+=['-I'+str(x) for x in [r/'3ds',r/'common',app/'include',core/'lib/include',r/'android/app/src/main/cpp/generated']]
 cmd+=[str(p/'check.c'),str(r/'3ds/controls.c'),str(app/'source/input_3ds.c')]
 cmd += [str(core/'lib/src'/f) for f in ['feedbacksender.c','feedback.c','controller.c','thread.c','time.c','log.c']]+['-lm','-o',str(p/'check')]
 subprocess.run(cmd,check=True)
 result=json.loads(subprocess.check_output([p/'check'],text=True))
 (r/'output/trigger-release-tests.json').write_text(json.dumps(result,indent=2)+'\n')
 # Prove the regression catches the original defect using the preserved baseline.
 baseline=args.baseline
 if baseline is not None:
  shutil.copytree(core/'lib/include',p/'baseline-include')
  shutil.copyfile(baseline/'lib/include/chiaki/feedbacksender.h',p/'baseline-include/chiaki/feedbacksender.h')
  broken=cmd.copy();broken[broken.index('-I'+str(core/'lib/include')):broken.index('-I'+str(core/'lib/include'))]=['-I'+str(p/'baseline-include'),'-DEXPECT_ORIGINAL']
  broken[broken.index(str(core/'lib/src/feedbacksender.c'))]=str(baseline/'lib/src/feedbacksender.c')
  subprocess.run(broken,check=True)
  failed=subprocess.run([p/'check'],capture_output=True,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)))
  assert failed.returncode!=0 and b'atomic_load(&remote_value)==0&&atomic_load(&dropped)==losses' in failed.stderr,'Original sender did not fail the expected release-loss assertion'
print('PASS actual sender: L2/R2/face-button releases recover, 1/4/8 lost packets, idle/live steering, newer press priority, bounded refresh and shutdown')
print(json.dumps(result,indent=2))
