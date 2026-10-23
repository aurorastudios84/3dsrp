#include "audio_pipeline.h"
#include <3ds.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/thread.h>
#include <chiaki/time.h>
#include <opus/opus.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define PCM_SAMPLES 5760u
#define PCM_CHANNELS 2u
/* One packet can deliver 15 source frames before the worker runs. Keep the
 * callback nonblocking while bounding burst storage and whole-frame drops. */
#define PCM_SLOTS 16u
#define WAVE_SLOTS 3u
#define PACKET_MAX 4096u
#define PREFILL_MS 30u
#define OUTPUT_PRIORITY 0x2f
#define PCM_BYTES (PCM_SAMPLES * PCM_CHANNELS * sizeof(int16_t))
/* App errors are separate from native negative Result/Opus errors. */
enum { AUDIO_HEADER=-1201, AUDIO_CHANGE=-1202, AUDIO_PACKET=-1203,
       AUDIO_PCM=-1204, AUDIO_ALLOC=-1205, AUDIO_THREAD=-1206, AUDIO_OWNER=-1207 };
typedef struct { int16_t pcm[PCM_SAMPLES*PCM_CHANNELS]; unsigned samples; } PcmSlot;
struct AudioPipeline {
    ChiakiLog *log;
    ChiakiOpusDecoder decoder;
    ChiakiAudioSink decoder_sink;
    ChiakiAudioHeader header; /* producer writes once before header_ready release */
    ChiakiThread worker;
    LightEvent output_event;
    bool worker_live, decoder_live;
    PcmSlot slots[PCM_SLOTS];
    atomic_uint head,tail,received,decoded,queued,dropped,underruns,errors;
    atomic_uint rate,channels;
    atomic_int last_error;
    atomic_bool accepting,stopping,done,running,failed,header_ready;
};
/* libctru publishes callback/function data without locking. Ignore that pair's
 * data field and publish the sole NDSP owner atomically before installation. */
static _Atomic(AudioPipeline *) callback_owner;
static void ndsp_frame_ready(void *unused)
{
    (void)unused;
    AudioPipeline *p=atomic_load_explicit(&callback_owner,memory_order_acquire);
    if(p) {
        LightEvent_Signal(&p->output_event);
    }
}
static void output_wait(AudioPipeline *p)
{
    /* Retained oneshot signals cover publication before the wait. The timeout
     * permits periodic rechecking even if native callbacks cease. */
    if(!atomic_load(&p->stopping) && !atomic_load(&p->failed))
        LightEvent_WaitTimeout(&p->output_event,1000000000);
}
/* Only the consumer calls this, without advancing tail during the snapshot.
 * The producer cannot reuse any of these published slots until tail advances. */
static unsigned pending_samples(AudioPipeline *p)
{
    unsigned tail=atomic_load_explicit(&p->tail,memory_order_relaxed);
    unsigned head=atomic_load_explicit(&p->head,memory_order_acquire),samples=0;
    for(unsigned i=0;i<head-tail;i++) samples+=p->slots[(tail+i)%PCM_SLOTS].samples;
    return samples;
}
static void audio_error(AudioPipeline *p, int error, bool fatal)
{
    atomic_fetch_add(&p->errors,1);
    atomic_store(&p->last_error,error);
    if(fatal) {
        atomic_store(&p->failed,true); atomic_store(&p->accepting,false);
        LightEvent_Signal(&p->output_event);
    }

}
static bool valid_header(const ChiakiAudioHeader *h)
{
    if(!h || h->bits!=16 || (h->channels!=1 && h->channels!=2)) return false;
    if(h->rate!=8000 && h->rate!=12000 && h->rate!=16000 && h->rate!=24000 && h->rate!=48000) return false;
    return h->frame_size && h->frame_size<=PCM_SAMPLES && h->frame_size<=h->rate*120u/1000u;
}
static void pcm_frame(int16_t *pcm, size_t samples, void *user)
{
    AudioPipeline *p=user;
    if(!atomic_load(&p->accepting)) return;
    if(!pcm || !samples || samples>p->header.frame_size || samples>PCM_SAMPLES) {
        audio_error(p,AUDIO_PCM,false); atomic_fetch_add(&p->dropped,1); return;
    }
    atomic_fetch_add(&p->decoded,1);
    unsigned head=atomic_load_explicit(&p->head,memory_order_relaxed);
    if(head-atomic_load_explicit(&p->tail,memory_order_acquire)>=PCM_SLOTS) {
        atomic_fetch_add(&p->dropped,1); return;
    }
    PcmSlot *slot=&p->slots[head%PCM_SLOTS];
    memcpy(slot->pcm,pcm,samples*p->header.channels*sizeof(int16_t));
    slot->samples=(unsigned)samples;
    atomic_store_explicit(&p->head,head+1,memory_order_release);
    LightEvent_Signal(&p->output_event);
}
static void sink_header(ChiakiAudioHeader *h, void *user)
{
    AudioPipeline *p=user;
    if(!atomic_load(&p->accepting)) return;
    if(!valid_header(h)) { audio_error(p,AUDIO_HEADER,true); return; }
    if(atomic_load(&p->header_ready)) {
        if(h->channels!=p->header.channels || h->bits!=p->header.bits ||
           h->rate!=p->header.rate || h->frame_size!=p->header.frame_size)
            audio_error(p,AUDIO_CHANGE,true);
        return;
    }
    p->header=*h;
    p->decoder_sink.header_cb(h,p->decoder_sink.user);
    if(!p->decoder.opus_decoder || !p->decoder.pcm_buf) { audio_error(p,AUDIO_ALLOC,true); return; }
    atomic_store(&p->rate,h->rate); atomic_store(&p->channels,h->channels);
    atomic_store_explicit(&p->header_ready,true,memory_order_release);
    LightEvent_Signal(&p->output_event);
}
static void sink_frame(uint8_t *data, size_t size, void *user)
{
    AudioPipeline *p=user;
    atomic_fetch_add(&p->received,1);
    if(!atomic_load(&p->accepting)) { atomic_fetch_add(&p->dropped,1); return; }
    /* The receiver requests Opus packet-loss concealment with exactly NULL,0. */
    bool conceal=!data && !size;
    if(!atomic_load(&p->header_ready) || (!conceal && (!data || !size)) || size>PACKET_MAX) {
        audio_error(p,AUDIO_PACKET,false); atomic_fetch_add(&p->dropped,1); return;
    }
    if(!conceal) {
        int samples=opus_packet_get_nb_samples(data,(opus_int32)size,p->header.rate);
        if(samples<=0 || (unsigned)samples>p->header.frame_size) {
            audio_error(p,samples<0?samples:AUDIO_PACKET,false); atomic_fetch_add(&p->dropped,1); return;
        }
    }
    unsigned before=atomic_load(&p->decoded);
    p->decoder_sink.frame_cb(data,size,p->decoder_sink.user);
    if(atomic_load(&p->decoded)==before) {
        audio_error(p,AUDIO_PACKET,false); atomic_fetch_add(&p->dropped,1);
    }
}
static void *output_worker(void *user)
{
    AudioPipeline *p=user;
    ndspWaveBuf waves[WAVE_SLOTS];
    void *storage[WAVE_SLOTS]={0};
    bool initialized=false,configured=false,played=false,owns_callback=false;
    bool priming=true;
    memset(waves,0,sizeof(waves));
    Result priority_result=svcSetThreadPriority(CUR_THREAD_HANDLE,OUTPUT_PRIORITY);
    if(R_FAILED(priority_result)) audio_error(p,priority_result,false);
    s32 effective_priority;
    priority_result=svcGetThreadPriority(&effective_priority,CUR_THREAD_HANDLE);
    if(R_FAILED(priority_result)) audio_error(p,priority_result,false);
    AudioPipeline *expected_owner=NULL;
    owns_callback=atomic_compare_exchange_strong(&callback_owner,&expected_owner,p);
    if(!owns_callback) { audio_error(p,AUDIO_OWNER,true); goto cleanup; }
    Result result=ndspInit();
    if(R_FAILED(result)) { audio_error(p,result,true); goto cleanup; }
    initialized=true;
    ndspSetCallback(ndsp_frame_ready,NULL);

    ndspChnReset(0);
    ndspChnSetPaused(0,true);
    for(unsigned i=0;i<WAVE_SLOTS;i++) {
        storage[i]=linearAlloc(PCM_BYTES);
        if(!storage[i]) { audio_error(p,AUDIO_ALLOC,true); goto cleanup; }
        waves[i].data_vaddr=storage[i];
    }
    atomic_store(&p->running,true);
    while(!atomic_load(&p->stopping) && !atomic_load(&p->failed)) {
        if(!configured && atomic_load_explicit(&p->header_ready,memory_order_acquire)) {
            float mix[12]={0}; mix[0]=1.0f; mix[1]=1.0f;
            ndspChnSetFormat(0,p->header.channels==1?NDSP_FORMAT_MONO_PCM16:NDSP_FORMAT_STEREO_PCM16);
            ndspChnSetInterp(0,NDSP_INTERP_LINEAR);
            ndspChnSetRate(0,(float)p->header.rate); ndspChnSetMix(0,mix);
            configured=true;
        }
        if(!configured) { output_wait(p); continue; }
        unsigned free_waves=0;
        for(unsigned i=0;i<WAVE_SLOTS;i++)
            if(waves[i].status==NDSP_WBUF_FREE || waves[i].status==NDSP_WBUF_DONE) free_waves++;
        if(played && free_waves==WAVE_SLOTS && !priming) {
            priming=true;
            atomic_fetch_add(&p->underruns,1);
            ndspChnSetPaused(0,true);
        }
        unsigned target_samples=(p->header.rate*PREFILL_MS+999)/1000;
        if(priming && pending_samples(p)<target_samples) { output_wait(p); continue; }
        bool submitted=false;
        for(unsigned i=0;i<WAVE_SLOTS;i++) {
            if(waves[i].status!=NDSP_WBUF_FREE && waves[i].status!=NDSP_WBUF_DONE) continue;
            if(atomic_load(&p->stopping) || atomic_load(&p->failed)) break;
            unsigned tail=atomic_load_explicit(&p->tail,memory_order_relaxed);
            if(tail==atomic_load_explicit(&p->head,memory_order_acquire)) continue;
            PcmSlot *slot=&p->slots[tail%PCM_SLOTS];
            size_t bytes=slot->samples*p->header.channels*sizeof(int16_t);
            memcpy(storage[i],slot->pcm,bytes);
            waves[i].nsamples=slot->samples;
            result=DSP_FlushDataCache(storage[i],bytes);
            if(R_FAILED(result)) { audio_error(p,result,true); break; }
            ndspChnWaveBufAdd(0,&waves[i]);
            atomic_store_explicit(&p->tail,tail+1,memory_order_release);
            atomic_fetch_add(&p->queued,1);
            submitted=true;
        }
        if(submitted && priming && !atomic_load(&p->stopping) && !atomic_load(&p->failed)) {
            ndspChnSetPaused(0,false);
            priming=false;
            played=true;
        }
        output_wait(p);
    }
cleanup:
    /* This thread alone owns NDSP and all memory visible to its DMA worker. */
    /* Keep the owner/event alive through ndspExit's native-thread join. Never
     * unregister while libctru could be between its unlocked check and call.
     * Retain the owner claim until unregister finishes, excluding another
     * pipeline from taking the shared NDSP channel during cleanup. */
    if(initialized) {
        do { ndspChnWaveBufClear(0); } while(0);
        do { ndspChnReset(0); } while(0);
        do { ndspExit(); } while(0);
        do { ndspSetCallback(NULL,NULL); } while(0);
         /* native callback thread has joined */
    }
    if(owns_callback) atomic_store_explicit(&callback_owner,NULL,memory_order_release);
    for(unsigned i=0;i<WAVE_SLOTS;i++) if(storage[i]) do { linearFree(storage[i]); } while(0);
    atomic_store(&p->running,false);
    do { atomic_store_explicit(&p->done,true,memory_order_release); } while(0);
    return NULL;
}
AudioPipeline *audio_pipeline_create(ChiakiLog *log)
{
    AudioPipeline *p=calloc(1,sizeof(*p));
    if(p) p->log=log;
    return p;
}
bool audio_pipeline_start(AudioPipeline *p)
{
    if(!p || p->worker_live || p->decoder_live) return false;
    LightEvent_Init(&p->output_event,RESET_ONESHOT);
    atomic_store(&p->head,0); atomic_store(&p->tail,0);
    atomic_store(&p->received,0); atomic_store(&p->decoded,0); atomic_store(&p->queued,0);
    atomic_store(&p->dropped,0); atomic_store(&p->underruns,0); atomic_store(&p->errors,0);
    atomic_store(&p->rate,0); atomic_store(&p->channels,0); atomic_store(&p->last_error,0);
    atomic_store(&p->stopping,false); atomic_store(&p->done,false); atomic_store(&p->running,false);
    atomic_store(&p->failed,false); atomic_store(&p->header_ready,false);
    memset(&p->header,0,sizeof(p->header));
    chiaki_opus_decoder_init(&p->decoder,p->log); p->decoder_live=true;
    chiaki_opus_decoder_set_cb(&p->decoder,NULL,pcm_frame,p);
    chiaki_opus_decoder_get_sink(&p->decoder,&p->decoder_sink);
    atomic_store(&p->accepting,true);
    p->worker_live=chiaki_thread_create(&p->worker,output_worker,p)==CHIAKI_ERR_SUCCESS;
    if(!p->worker_live) {
        audio_error(p,AUDIO_THREAD,true);
        do { chiaki_opus_decoder_fini(&p->decoder); } while(0); p->decoder_live=false;
    }
    return p->worker_live;
}
void audio_pipeline_get_sink(AudioPipeline *p, ChiakiAudioSink *sink)
{
    if(sink) *sink=(ChiakiAudioSink){.user=p,.header_cb=p?sink_header:NULL,.frame_cb=p?sink_frame:NULL};
}
void audio_pipeline_stop(AudioPipeline *p)
{
    if(!p) return;
    do { atomic_store(&p->accepting,false); } while(0); do { atomic_store(&p->stopping,true); } while(0);
    do { LightEvent_Signal(&p->output_event); } while(0);
}
bool audio_pipeline_join(AudioPipeline *p)
{
    if(!p || !p->worker_live) return true;
    if(!atomic_load_explicit(&p->done,memory_order_acquire)) return false;
    ChiakiErrorCode joined=chiaki_thread_timedjoin(&p->worker,NULL,0);
    if(joined!=CHIAKI_ERR_SUCCESS) return false;
    p->worker_live=false;
    if(p->decoder_live) { do { chiaki_opus_decoder_fini(&p->decoder); } while(0); p->decoder_live=false; }
    return true;
}
AudioStats audio_pipeline_stats(AudioPipeline *p)
{
    if(!p) return (AudioStats){0};
    unsigned tail=atomic_load(&p->tail),head=atomic_load(&p->head),depth=head-tail;
    if(depth>PCM_SLOTS) depth=PCM_SLOTS;
    return (AudioStats){atomic_load(&p->received),atomic_load(&p->decoded),atomic_load(&p->queued),
        atomic_load(&p->dropped),atomic_load(&p->underruns),atomic_load(&p->errors),
        atomic_load(&p->rate),atomic_load(&p->channels),depth,atomic_load(&p->last_error),
        atomic_load(&p->running),atomic_load(&p->failed)};
}
void audio_pipeline_destroy(AudioPipeline *p)
{
    if(p && !p->worker_live && !p->decoder_live) free(p);
}
