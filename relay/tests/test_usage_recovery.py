#!/usr/bin/env python3
"""Production C accounting/transport with boundary saves, live replay and interrupted delivery."""
from pathlib import Path
import subprocess,tempfile,shutil,sys,struct
root=Path(__file__).resolve().parents[1];app=root/'3ds/shared'
c=r'''
#include "usage_relay.h"
#include "usage_platform.h"
#include "wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static uint8_t sent[128];static unsigned reports,cancels;static uint32_t serial;
int64_t usage_platform_epoch(void){return 1793836800;}
bool usage_random_uuid(char uuid[37]){static unsigned n=0;uint8_t bytes[16]={0};bytes[15]=++n;usage_uuid_from_random(uuid,bytes);return true;}
static void send_packet(unsigned t,const uint8_t *p,unsigned n,uint32_t stamp){
 if(t==RELAY_USAGE_CANCEL){cancels++;return;}
 assert(t==RELAY_USAGE_REPORT&&n==96);serial=stamp;memcpy(sent,p,n);reports++;
}
static void ack(uint32_t run,uint32_t seq,int result){uint8_t p[40];relay_header(p,RELAY_USAGE_ACK,99,0,0,1,8,0,0,seq);w32(p+32,run);w32(p+36,result);usage_relay_receive(p,40);}
static void dirs(void){mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/3dsrp-relay",0700);}
static unsigned saved_ms(void){
 FILE *f=fopen("sdmc:/3ds/3dsrp-relay/usage-pending.bin","rb");assert(f);uint8_t p[108];assert(fread(p,1,108,f)==108);fclose(f);return p[100]|p[101]<<8|p[102]<<16|p[103]<<24;
}
int main(int argc,char **argv){assert(argc==2||argc==3);dirs();

 if(!strcmp(argv[1],"checkpoint-kill")){
  usage_relay_init(10,send_packet);assert(usage_relay_choose(true));
  usage_relay_frame(99,1000);assert(saved_ms()==0);usage_relay_tick(true,true,1001);assert(!reports);
  usage_relay_frame(99,5999);assert(saved_ms()==0);usage_relay_tick(true,true,6000);assert(!reports);
  usage_relay_frame(99,6000);assert(saved_ms()==5000);usage_relay_tick(true,true,6001);assert(!reports);
  usage_relay_frame(99,91099);assert(saved_ms()==90099);usage_relay_tick(true,true,91100);assert(!reports); // Even retry timer cannot upload current gameplay.
  usage_relay_frame(99,92099);assert(saved_ms()==90099);
  _exit(0); // No boundary/shutdown: only the most recent local checkpoint survives.
 }
 if(!strcmp(argv[1],"recover")){
  usage_relay_init(11,send_packet);assert(usage_relay_allowed());
  usage_relay_tick(true,true,1000);assert(reports==1&&r32(sent+88)==90); // Previous run recovered immediately, even LIVE.
  uint32_t original=serial;
  usage_relay_frame(100,1100);assert(!cancels); // Starting new video must not cancel recovery.
  ack(10,original,1);usage_relay_tick(true,true,1101);assert(reports==1); // Stale old-run ACK ignored.
  ack(11,original,1);usage_relay_tick(true,true,1102);usage_relay_tick(true,true,1103);
  assert(reports==1&&saved_ms()==0); // Old report removed; current local checkpoint retained.
  usage_relay_frame(100,6100);usage_relay_tick(true,true,6101);assert(reports==1); // Current gameplay is checkpointed locally, never uploaded.
  usage_relay_tick(true,false,6102);assert(reports==2&&r32(sent+88)==5&&saved_ms()==5000); // Boundary saves final time.
  ack(11,serial,-1);usage_relay_tick(true,false,6103);unsigned attempts=reports;
  usage_relay_tick(false,false,6104);usage_relay_tick(true,true,7000);assert(reports==attempts+1&&r32(sent+88)==5); // Reconnect retries without another game.
  uint32_t retry=serial;usage_relay_tick(true,true,13000); // UDP/HTTP timeout retains data.
  usage_relay_tick(true,true,23000);assert(reports==attempts+2&&serial!=retry&&r32(sent+88)==5); // Existing transient retry preserved.
  ack(11,serial,1);usage_relay_tick(true,false,23001);unsigned confirmed=reports;
  usage_relay_tick(true,false,39000);usage_relay_tick(true,false,55000);assert(reports==confirmed); // Retry timer must not resend confirmed unchanged snapshots.
  usage_relay_shutdown(false);assert(saved_ms()==5000);return 0;
 }
 if(!strcmp(argv[1],"phone-first")){
  usage_relay_init(12,send_packet);assert(usage_relay_choose(true));
  usage_relay_frame(99,1000);usage_relay_frame(99,92099);
  usage_relay_tick(false,false,96000);usage_relay_shutdown(false);assert(!reports&&saved_ms()==91099);return 0;
 }
 if(!strcmp(argv[1],"handheld-first")){
  usage_relay_init(12,send_packet);assert(usage_relay_choose(true));
  usage_relay_frame(99,1000);usage_relay_frame(99,92099);usage_relay_shutdown(true);
  assert(reports&&r32(sent+88)==91&&saved_ms()==91099);return 0; // Phone can die immediately; SD still authoritative.
 }
 if(!strcmp(argv[1],"replay-closed")){
  usage_relay_init(13,send_packet);usage_relay_tick(true,true,1000);assert(reports==1&&r32(sent+88)==91);
  ack(13,serial,1);usage_relay_tick(true,true,1001);
  assert(access("sdmc:/3ds/3dsrp-relay/usage-pending.bin",F_OK)!=0);return 0;
 }
 if(!strcmp(argv[1],"inspect-owner")){
  usage_relay_init(14,send_packet);usage_relay_tick(true,true,1000);assert(argc==3&&reports==1&&r32(sent+88)==strtoul(argv[2],NULL,10));return 0;
 }
 return 2;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);(tmp/'test.c').write_text(c)
 (tmp/'3ds.h').write_text('typedef int LightLock;static void LightLock_Lock(LightLock *p){}static void LightLock_Unlock(LightLock *p){}')
 files=[root/'3ds/usage_relay.c']+[app/'source'/f'{name}.c' for name in ['usage_stats','usage_preferences','usage_journal','pair_store']]
 subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(tmp),'-I'+str(root/'3ds'),'-I'+str(root/'common'),'-I'+str(app/'include'),str(tmp/'test.c'),*map(str,files),'-o',str(tmp/'test')],check=True)
 def run(mode,dest,*args):subprocess.run([str(tmp/'test'),mode,*args],cwd=dest,check=True)
 for first,second in [('checkpoint-kill','recover'),('phone-first','replay-closed'),('handheld-first','replay-closed')]:
  dest=tmp/first;dest.mkdir();run(first,dest);run(second,dest)
 # If the owner's read-only SD snapshot exists, verify compatibility without modifying it or submitting data.
 owner=root/'output/usage37-investigation'
 if '--owner-snapshot' in sys.argv:
  dest=tmp/'owner';target=dest/'sdmc:/3ds/3dsrp-relay';target.mkdir(parents=True)
  for name in ['usage.bin','usage-pending.bin']:shutil.copyfile(owner/name,target/name)
  expected=struct.unpack_from('<I',(owner/'usage-pending.bin').read_bytes(),100)[0]//1000
  run('inspect-owner',dest,str(expected))
print('PASS five-second local checkpoints with no periodic gameplay uploads; crash/relaunch recovery; both shutdown orders; one accepted previous-run replay; live recovery; stale/lost ACKs; reconnect and transient retries; release37 journal compatibility')
