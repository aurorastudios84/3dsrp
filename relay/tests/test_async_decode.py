#!/usr/bin/env python3
"""Exercise the production decoder handoff with a deliberately blocked decoder."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / '3ds/main.c').read_text()
worker = source[source.index('static void decode_worker('):source.index('static bool get_phone(')]
main = source[source.index('int main(void)'):]
assert 'decode_picture(' not in main
assert 'relay_controls_sample(' in main and 'RELAY_INPUT_FULL' in main
assert main.index('threadJoin(decoder,') < main.index('if(sock>=0)close(sock)')

harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
typedef uint64_t u64;
typedef pthread_mutex_t LightLock;
#define LightLock_Lock pthread_mutex_lock
#define LightLock_Unlock pthread_mutex_unlock
static LightLock picture_lock=PTHREAD_MUTEX_INITIALIZER,decoded_lock=PTHREAD_MUTEX_INITIALIZER;
static atomic_bool stopping;
static atomic_uint link_error,current_session=1,decode_attempts,decode_us,decode_errors,stale_decodes,decoded_replaced,decoded_pictures;
static uint8_t pending_jpeg[32],decode_jpeg[32];
static uint16_t rgb[400*225],ready_rgb[400*225],display_rgb[400*225];
static unsigned pending_bytes,pending_width,pending_height,pending_session;
static unsigned ready_width,ready_height,ready_session;
static u64 pending_tick,ready_tick;
static bool ready_picture;
static sem_t entered,release_decode;
static void svcSleepThread(u64 ns){usleep(ns/1000);}
static u64 svcGetSystemTick(void){return 1;}
static unsigned micros(u64 tick){return 1;}
static bool decode_picture(const uint8_t *data,unsigned size,unsigned w,unsigned h){
 assert(size==1);sem_post(&entered);sem_wait(&release_decode);
 if(data[0]==9)return false;
 for(unsigned i=0;i<w*h;i++)rgb[i]=data[0];
 return true;
}
PRODUCTION_WORKER
static void *run(void *arg){decode_worker(arg);return NULL;}
static void feed(unsigned value){
 LightLock_Lock(&picture_lock);
 pending_jpeg[0]=value;pending_bytes=1;pending_width=400;pending_height=225;
 pending_session=atomic_load(&current_session);pending_tick=value;
 LightLock_Unlock(&picture_lock);
}
static void wait_count(atomic_uint *counter,unsigned target){
 for(unsigned i=0;i<5000&&atomic_load(counter)<target;i++)usleep(1000);
 assert(atomic_load(counter)==target);
}
int main(void){
 sem_init(&entered,0,0);sem_init(&release_decode,0,0);
 pthread_t thread;assert(!pthread_create(&thread,NULL,run,NULL));
 unsigned w,h,session;u64 tick;
 feed(1);sem_wait(&entered);
 // Decode is blocked: the UI can claim without waiting on its work.
 for(unsigned i=0;i<1000;i++)assert(!claim_picture(&w,&h,&session,&tick));
 // Receiver can replace pending compressed work during the blocked decode.
 feed(2);feed(3);sem_post(&release_decode);sem_wait(&entered);
 assert(claim_picture(&w,&h,&session,&tick)&&tick==1&&w==400&&h==225&&session==1);
 for(unsigned i=0;i<w*h;i++)assert(display_rgb[i]==1);
 sem_post(&release_decode);wait_count(&decoded_pictures,2);
 // Completed output is replaced rather than forming a display FIFO.
 feed(4);sem_wait(&entered);sem_post(&release_decode);wait_count(&decoded_pictures,3);
 assert(atomic_load(&decoded_replaced)==1);
 assert(claim_picture(&w,&h,&session,&tick)&&tick==4);
 for(unsigned i=0;i<w*h;i++)assert(display_rgb[i]==4);
 assert(!claim_picture(&w,&h,&session,&tick));
 // In-flight old-session output and decode errors must not be published.
 feed(5);sem_wait(&entered);atomic_store(&current_session,2);sem_post(&release_decode);
 wait_count(&stale_decodes,1);assert(!claim_picture(&w,&h,&session,&tick));
 feed(9);sem_wait(&entered);sem_post(&release_decode);wait_count(&decode_errors,1);
 assert(!claim_picture(&w,&h,&session,&tick));
 // Stop while decoding: finish safely, publish nothing, then join.
 feed(6);sem_wait(&entered);atomic_store(&stopping,true);sem_post(&release_decode);
 assert(!pthread_join(thread,NULL));assert(!claim_picture(&w,&h,&session,&tick));
 assert(atomic_load(&decoded_pictures)==3);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / 'check.c').write_text(harness.replace('PRODUCTION_WORKER', worker))
    subprocess.run(['cc', '-O1', '-g', '-pthread', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(p / 'check.c'), '-o', str(p / 'check')], check=True)
    subprocess.run([str(p / 'check')], check=True, timeout=20)
print('PASS production async decode: nonblocking UI handoff during stalled decode, newest-picture replacement, intact pixels, session rejection, errors and joined shutdown')
