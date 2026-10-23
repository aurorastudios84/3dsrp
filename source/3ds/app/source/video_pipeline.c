#include "video_pipeline.h"
#include "video_format.h"
#include "mvd_sink.h"
#include "app.h"
#ifdef N3DS_GPU_BILINEAR
#include "gpu_video.h"
#endif
#include <chiaki/thread.h>
#include <chiaki/time.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VIDEO_FRAME_CAPACITY 3u
#define VIDEO_SOURCE_PIXELS (960u*540u)
typedef struct {
    uint16_t pixels[VIDEO_SOURCE_PIXELS];
    unsigned width,height;
} SourceFrame;
#define VIDEO_HELD_STEPS_MAX 32u
#define VIDEO_HELD_MS_MAX 250u

typedef struct {
    uint8_t data[VIDEO_MAX_AU];
    size_t size;
    unsigned width,height,epoch;
    bool picture,idr,parameter;
} VideoSlot;
struct VideoPipeline {
#ifdef N3DS_GPU_BILINEAR
    GpuVideo *gpu;
    bool gpu_pending;
#endif
    VideoSlot slots[VIDEO_QUEUE_CAPACITY];
    SourceFrame display[VIDEO_FRAME_CAPACITY],retained;
    /* UI-owned composition planes. Never clear/scale into active scanout;
     * publish only completed images with the legacy contiguous copy pattern. */
    uint16_t composed_top[VIDEO_SCREEN_PIXELS],composed_bottom[ZOOM_BOTTOM_PIXELS];
    bool have_retained,last_zoom;
    ZoomPresentation last_presentation;
    ChiakiThread worker;
    bool worker_live;
    atomic_bool accepting, stopping, idle, cleanup_requested, done, failed, awaiting_idr;
    atomic_uint head, tail, epoch;
    atomic_uint received, decoded, presented, dropped, errors, width, height;
    atomic_uint frame_head,frame_tail;
};

static void resynchronize(VideoPipeline *pipeline)
{
    if(!atomic_exchange(&pipeline->awaiting_idr,true)) atomic_fetch_add(&pipeline->epoch,1);
}
static void *decoder_worker(void *user)
{
    VideoPipeline *pipeline=user;

    bool initialized=false;
    unsigned width=0,height=0;
    while(!atomic_load(&pipeline->stopping)) {
        unsigned tail=atomic_load_explicit(&pipeline->tail,memory_order_relaxed);
        if(tail==atomic_load_explicit(&pipeline->head,memory_order_acquire)) { usleep(2000); continue; }
        VideoSlot *slot=&pipeline->slots[tail%VIDEO_QUEUE_CAPACITY];
        bool obsolete=slot->picture && slot->epoch!=atomic_load(&pipeline->epoch);
        if(obsolete && !slot->parameter) {
            atomic_fetch_add(&pipeline->dropped,1);
            atomic_store_explicit(&pipeline->tail,tail+1,memory_order_release);
            continue;
        }
        int result=0;
        if(!initialized) {
            width=slot->width; height=slot->height;
            VideoAccessUnit first;
            VideoSps sps;
            bool found_sps=false;
            if(video_parse_au(slot->data,slot->size,&first)) {
                for(unsigned index=0;index<first.count;index++) {
                    VideoNal *nal=&first.nals[index];
                    if(nal->type==7 && video_parse_sps(slot->data+nal->offset+nal->prefix,
                            nal->size-nal->prefix,&sps) &&
                       video_sps_safe(&sps,width,height,width==960 ? RP_ENV_540_MAX_MBS : RP_ENV_MAX_MBS_DEFAULT,
                            width==960 ? RP_ENV_540_MAX_DPB : RP_ENV_MAX_DPB_DEFAULT,
                            width==960 ? RP_ENV_540_MAX_LEVEL : RP_ENV_MAX_LEVEL_DEFAULT)) {
                        found_sps=true; break;
                    }
                }
            }

            result=found_sps ? mvd_sink_start(width,height,sps.level) : -9;

            initialized=result==0;
            if(initialized) { atomic_store(&pipeline->width,width); atomic_store(&pipeline->height,height); }
        } else if(slot->width!=width || slot->height!=height) result=-10;
        unsigned held_steps=0;
        uint32_t held_since=(uint32_t)chiaki_time_now_monotonic_ms();
        while(result==0 && !atomic_load(&pipeline->stopping)) {
            uint32_t waiting=(uint32_t)chiaki_time_now_monotonic_ms();
            while(atomic_load_explicit(&pipeline->frame_head,memory_order_relaxed)-
                  atomic_load_explicit(&pipeline->frame_tail,memory_order_acquire)>=VIDEO_FRAME_CAPACITY &&
                  !atomic_load(&pipeline->stopping)) {
                if((uint32_t)chiaki_time_now_monotonic_ms()-waiting>=VIDEO_HELD_MS_MAX) { result=-16; break; }
                usleep(2000);
            }
            if(result<0 || atomic_load(&pipeline->stopping)) break;

            result=mvd_sink_feed_au(slot->data,slot->size,!obsolete);

            if(result==MVD_FEED_CONSUMED_FRAME || result==MVD_FEED_RETRY_FRAME) {
                unsigned frame=atomic_load_explicit(&pipeline->frame_head,memory_order_relaxed);
                SourceFrame *output=&pipeline->display[frame%VIDEO_FRAME_CAPACITY];
                if(!mvd_sink_copy_source(output->pixels,VIDEO_SOURCE_PIXELS,&output->width,&output->height)) {
                    result=-18; break;
                }
                atomic_store_explicit(&pipeline->frame_head,frame+1,memory_order_release);
                atomic_fetch_add(&pipeline->decoded,1);
            }
            if(result==MVD_FEED_RETRY_EMPTY || result==MVD_FEED_RETRY_FRAME) {
                held_steps++;
                if(held_steps>=VIDEO_HELD_STEPS_MAX ||
                   (uint32_t)chiaki_time_now_monotonic_ms()-held_since>=VIDEO_HELD_MS_MAX) result=-17;
                else result=0;
                continue;
            }
            if(result==MVD_FEED_CONSUMED_EMPTY || result==MVD_FEED_CONSUMED_FRAME) result=0;
            break;
        }
        if(result<0 && !atomic_load(&pipeline->stopping)) {
            atomic_fetch_add(&pipeline->errors,1);
            atomic_store(&pipeline->failed,true);
            do { atomic_store(&pipeline->accepting,false); } while(0);

            break;
        }
        atomic_store_explicit(&pipeline->tail,tail+1,memory_order_release);
    }
    /* An error may end the decode loop before quiesce. Acknowledge idle only
     * after cancellation, when no later process/render call can begin. */
    while(!atomic_load(&pipeline->stopping)) usleep(2000);

    atomic_store_explicit(&pipeline->idle,true,memory_order_release);
    while(!atomic_load(&pipeline->cleanup_requested)) usleep(2000);

    do { mvd_sink_stop(); } while(0);

    do { atomic_store(&pipeline->done,true); } while(0);

    return NULL;
}
VideoPipeline *video_pipeline_create(void)
{
    VideoPipeline *pipeline=calloc(1,sizeof(*pipeline));
#ifdef N3DS_GPU_BILINEAR
    if(pipeline && !(pipeline->gpu=gpu_video_create())) { free(pipeline); return NULL; }
#endif
    return pipeline;
}
bool video_pipeline_start(VideoPipeline *pipeline)
{
    if(!pipeline || pipeline->worker_live) return false;
    /* A fresh cancellation epoch begins only after the previous worker joined,
     * before the new worker can enter any native MVD call. */
    mvd_sink_reset_cancel();
    pipeline->have_retained=false; pipeline->last_zoom=false;
#ifdef N3DS_GPU_BILINEAR
    pipeline->gpu_pending=false;
#endif
    do { atomic_store(&pipeline->accepting,false); } while(0);
    atomic_store(&pipeline->stopping,false); atomic_store(&pipeline->idle,false);
    atomic_store(&pipeline->done,false);
    atomic_store(&pipeline->cleanup_requested,false);
    atomic_store(&pipeline->failed,false); atomic_store(&pipeline->awaiting_idr,true);
    atomic_store(&pipeline->head,0); atomic_store(&pipeline->tail,0); atomic_store(&pipeline->epoch,0);
    atomic_store(&pipeline->received,0); atomic_store(&pipeline->decoded,0); atomic_store(&pipeline->presented,0);
    atomic_store(&pipeline->dropped,0); atomic_store(&pipeline->errors,0);
    atomic_store(&pipeline->width,0); atomic_store(&pipeline->height,0);
    atomic_store(&pipeline->frame_head,0); atomic_store(&pipeline->frame_tail,0);
    pipeline->worker_live=chiaki_thread_create(&pipeline->worker,decoder_worker,pipeline)==CHIAKI_ERR_SUCCESS;
    atomic_store(&pipeline->accepting,pipeline->worker_live);
    return pipeline->worker_live;
}
bool video_pipeline_push(VideoPipeline *pipeline, const uint8_t *data, size_t size,
                          unsigned width, unsigned height, int32_t lost)
{
    if(!pipeline || !atomic_load(&pipeline->accepting)) return false;
    atomic_fetch_add(&pipeline->received,1);
    VideoAccessUnit unit;
    if(!width || !height || ((width>852 || height>480) && (width!=960 || height!=540)) || !video_parse_au(data,size,&unit)) {
        atomic_fetch_add(&pipeline->errors,1); atomic_store(&pipeline->failed,true);
        do { atomic_store(&pipeline->accepting,false); } while(0); return false;
    }
    if(lost>0) resynchronize(pipeline);
    if(unit.picture && atomic_load(&pipeline->awaiting_idr) && !unit.idr) {
        atomic_fetch_add(&pipeline->dropped,1); return false;
    }
    unsigned head=atomic_load_explicit(&pipeline->head,memory_order_relaxed);
    if(head-atomic_load_explicit(&pipeline->tail,memory_order_acquire)>=VIDEO_QUEUE_CAPACITY) {
        resynchronize(pipeline); atomic_fetch_add(&pipeline->dropped,1);
        if(unit.sps || unit.pps) {
            atomic_fetch_add(&pipeline->errors,1); atomic_store(&pipeline->failed,true);
            do { atomic_store(&pipeline->accepting,false); } while(0);
        }
        return false;
    }
    VideoSlot *slot=&pipeline->slots[head%VIDEO_QUEUE_CAPACITY];
    memcpy(slot->data,data,size);
    slot->size=size; slot->width=width; slot->height=height;
    slot->epoch=atomic_load(&pipeline->epoch); slot->picture=unit.picture; slot->idr=unit.idr;
    slot->parameter=unit.sps || unit.pps;
    atomic_store_explicit(&pipeline->head,head+1,memory_order_release);
    if(unit.idr) atomic_store(&pipeline->awaiting_idr,false);
    return true;
}
void video_pipeline_quiesce(VideoPipeline *pipeline)
{
    if(!pipeline) return;
    do { atomic_store(&pipeline->accepting,false); } while(0);
    do { atomic_store(&pipeline->stopping,true); } while(0);
    do { mvd_sink_cancel(); } while(0);
}
bool video_pipeline_idle(VideoPipeline *pipeline)
{
    if(!pipeline || !pipeline->worker_live) return true;
    return atomic_load_explicit(&pipeline->idle,memory_order_acquire);
}
void video_pipeline_stop(VideoPipeline *pipeline)
{
    if(!pipeline) return;
    do { video_pipeline_quiesce(pipeline); } while(0);
    do { atomic_store(&pipeline->cleanup_requested,true); } while(0);
}
bool video_pipeline_join(VideoPipeline *pipeline)
{
    if(!pipeline || !pipeline->worker_live) return true;
    if(!atomic_load(&pipeline->done)) return false;

    ChiakiErrorCode result=chiaki_thread_timedjoin(&pipeline->worker,NULL,0);

    if(result!=CHIAKI_ERR_SUCCESS) return false;
    pipeline->worker_live=false;

    return true;
}
static bool consume_source(VideoPipeline *pipeline)
{
    unsigned tail=atomic_load_explicit(&pipeline->frame_tail,memory_order_relaxed);
    if(tail==atomic_load_explicit(&pipeline->frame_head,memory_order_acquire)) return false;
    SourceFrame *source=&pipeline->display[tail%VIDEO_FRAME_CAPACITY];
    pipeline->retained.width=source->width; pipeline->retained.height=source->height;
    memcpy(pipeline->retained.pixels,source->pixels,(size_t)source->width*source->height*sizeof(uint16_t));
    pipeline->have_retained=true;
    atomic_store_explicit(&pipeline->frame_tail,tail+1,memory_order_release);
    atomic_fetch_add(&pipeline->presented,1);
    return true;
}
static bool same_presentation(const ZoomPresentation *a,const ZoomPresentation *b)
{
    return a->view.center_x==b->view.center_x && a->view.center_y==b->view.center_y &&
           a->view.zoom_q==b->view.zoom_q && a->box==b->box && a->revision==b->revision &&
           a->mode_label==b->mode_label;
}
bool video_pipeline_present_zoom(VideoPipeline *pipeline,uint16_t *top,uint16_t *bottom,
                                 const ZoomPresentation *presentation)
{
    ZoomRect validated;
    if(!pipeline || (!top && !bottom) || !presentation ||
       !zoom_crop_rect(960,540,&presentation->view,&validated)) return false;
    bool consumed=consume_source(pipeline);
#ifdef N3DS_GPU_BILINEAR
    pipeline->gpu_pending|=consumed;
    if(!pipeline->have_retained || (!pipeline->gpu_pending && pipeline->last_zoom &&
       same_presentation(presentation,&pipeline->last_presentation))) return false;
    SourceFrame *frame=&pipeline->retained;
    if(bottom && !zoom_render_frame(frame->pixels,frame->width,frame->height,frame->width,
                                    presentation,NULL,pipeline->composed_bottom)) return false;
    if(top && !gpu_video_present(pipeline->gpu,frame->pixels,frame->width,frame->height,
                                pipeline->gpu_pending,presentation)) return false;
    if(top) pipeline->gpu_pending=false;
    if(bottom) memcpy(bottom,pipeline->composed_bottom,sizeof(pipeline->composed_bottom));
    pipeline->last_presentation=*presentation; pipeline->last_zoom=true;
    return true;
#else
    if(!pipeline->have_retained || (!consumed && pipeline->last_zoom &&
       same_presentation(presentation,&pipeline->last_presentation))) return false;
    SourceFrame *frame=&pipeline->retained;
    if(!zoom_render_frame(frame->pixels,frame->width,frame->height,frame->width,presentation,
                          top ? pipeline->composed_top : NULL,
                          bottom ? pipeline->composed_bottom : NULL)) return false;
    /* Complete both views before either caller framebuffer changes. */
    if(top) memcpy(top,pipeline->composed_top,sizeof(pipeline->composed_top));
    if(bottom) memcpy(bottom,pipeline->composed_bottom,sizeof(pipeline->composed_bottom));
    pipeline->last_presentation=*presentation; pipeline->last_zoom=true;
    return true;
#endif
}
bool video_pipeline_present(VideoPipeline *pipeline,uint16_t *framebuffer)
{
#ifdef N3DS_GPU_BILINEAR
    if(!pipeline || !framebuffer) return false;
    pipeline->gpu_pending|=consume_source(pipeline);
    if(!pipeline->have_retained || !pipeline->gpu_pending) return false;
    const ZoomPresentation presentation={.view={ZOOM_CENTER,ZOOM_CENTER,ZOOM_DEFAULT_Q},.box=false};
    SourceFrame *frame=&pipeline->retained;
    if(!gpu_video_present(pipeline->gpu,frame->pixels,frame->width,frame->height,true,&presentation)) return false;
    pipeline->gpu_pending=false; pipeline->last_zoom=false;
    return true;
#else
    if(!pipeline || !framebuffer || !consume_source(pipeline)) return false;
    const ZoomPresentation presentation={.view={ZOOM_CENTER,ZOOM_CENTER,ZOOM_DEFAULT_Q},.box=false};
    SourceFrame *frame=&pipeline->retained;
    pipeline->last_zoom=false;
    if(!zoom_render_frame(frame->pixels,frame->width,frame->height,frame->width,&presentation,
                          pipeline->composed_top,NULL)) return false;
    memcpy(framebuffer,pipeline->composed_top,sizeof(pipeline->composed_top));
    return true;
#endif
}
bool video_pipeline_clear_top(VideoPipeline *pipeline)
{
#ifdef N3DS_GPU_BILINEAR
    return pipeline && gpu_video_clear(pipeline->gpu);
#else
    return pipeline!=NULL;
#endif
}
bool video_pipeline_present_ui(VideoPipeline *pipeline,const uint16_t *rgb565)
{
#ifdef N3DS_GPU_BILINEAR
    const ZoomPresentation p={.view={ZOOM_CENTER,ZOOM_CENTER,ZOOM_DEFAULT_Q},.box=false};
    return pipeline && rgb565 && gpu_video_present(pipeline->gpu,rgb565,400,240,true,&p);
#else
    (void)pipeline;(void)rgb565;return false;
#endif
}
VideoStats video_pipeline_stats(VideoPipeline *pipeline)
{
    if(!pipeline) return (VideoStats){0};
    unsigned tail=atomic_load(&pipeline->tail),head=atomic_load(&pipeline->head);
    unsigned depth=head-tail;
    if(depth>VIDEO_QUEUE_CAPACITY) depth=VIDEO_QUEUE_CAPACITY;
    return (VideoStats){atomic_load(&pipeline->received),atomic_load(&pipeline->decoded),
        atomic_load(&pipeline->presented),atomic_load(&pipeline->dropped),atomic_load(&pipeline->errors),
        depth,atomic_load(&pipeline->width),
        atomic_load(&pipeline->height),pipeline->worker_live,atomic_load(&pipeline->failed)};
}
void video_pipeline_destroy(VideoPipeline *pipeline)
{
    if(pipeline && !pipeline->worker_live) {
#ifdef N3DS_GPU_BILINEAR
        gpu_video_destroy(pipeline->gpu);
#endif
        free(pipeline);
    }
}
