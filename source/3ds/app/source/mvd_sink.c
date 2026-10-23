#include "mvd_sink.h"
#include "mvd_reference_service.h"
#include "video_format.h"
#include "app.h"
#include <3ds.h>
#include <chiaki/time.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static u16 *pixels;
static u8 *nalbuf;
static void *pixels_allocation,*nal_allocation;
static size_t output_bytes;
static unsigned width,height,stride,padded_height;
static bool ready,validated_sps,have_sps,have_pps,first_picture,drain_pending;
static uint8_t sps_bytes[256],pps_bytes[256];
static size_t sps_size,pps_size;
static atomic_bool canceled;
static MVDSTD_Config config;
static unsigned empty_renders,drain_traces;
static bool first_nal,first_render,picture_started;
static uint32_t picture_since;

static bool cache_sync(const void *pointer,u32 size,unsigned operation)
{

    Result result=operation==3 ? GSPGPU_InvalidateDataCache(pointer,size) : GSPGPU_FlushDataCache(pointer,size);

    return R_SUCCEEDED(result);
}

static void *linear_alias(void *pointer)
{
    if(!pointer || ((uintptr_t)pointer&0xf0000000u)==0x30000000u) return pointer;
    void *converted=osConvertOldLINEARMemToNew(pointer);
    return converted ? converted : pointer;
}
static int mvd_level(unsigned level)
{
    switch(level) {
    case 9: return MVD_H264_LEVEL_1_0B;
    case 10: return MVD_H264_LEVEL_1_0;
    case 11: return MVD_H264_LEVEL_1_1;
    case 12: return MVD_H264_LEVEL_1_2;
    case 13: return MVD_H264_LEVEL_1_3;
    case 20: return MVD_H264_LEVEL_2_0;
    case 21: return MVD_H264_LEVEL_2_1;
    case 22: return MVD_H264_LEVEL_2_2;
    case 30: return MVD_H264_LEVEL_3_0;
    case 31: return MVD_H264_LEVEL_3_1;
    case 32: return MVD_H264_LEVEL_3_2;
    case 40: return MVD_H264_LEVEL_4_0;
    case 41: return MVD_H264_LEVEL_4_1;
    case 42: return MVD_H264_LEVEL_4_2;
    case 50: return MVD_H264_LEVEL_5_0;
    case 51: return MVD_H264_LEVEL_5_1;
    case 52: return MVD_H264_LEVEL_5_2;
    default: return -1;
    }
}
int mvd_sink_start(unsigned requested_width,unsigned requested_height,unsigned h264_level)
{
    mvd_sink_stop();
    first_nal=first_render=picture_started=validated_sps=have_sps=have_pps=drain_pending=false;
    first_picture=true; empty_renders=drain_traces=0; sps_size=pps_size=0;
    if(!requested_width || !requested_height || ((requested_width>852 || requested_height>480) && (requested_width!=960 || requested_height!=540))) return -1;
    int level=mvd_level(h264_level);
    if(level<0) return -9;
    width=requested_width; height=requested_height;
    stride=(width+15)&~15u; padded_height=(height+15)&~15u;
    output_bytes=(size_t)stride*padded_height*2;

    pixels_allocation=linearMemAlign(output_bytes,128);
    nal_allocation=linearMemAlign(VIDEO_MAX_AU+64,128);
    pixels=linear_alias(pixels_allocation); nalbuf=linear_alias(nal_allocation);
    if(!pixels || !nalbuf) { mvd_sink_stop(); return -2; }
    if(((uintptr_t)pixels&0xf0000000u)!=0x30000000u ||
       ((uintptr_t)nalbuf&0xf0000000u)!=0x30000000u ||
       !osConvertVirtToPhys(pixels) || !osConvertVirtToPhys(nalbuf)) { mvd_sink_stop(); return -3; }

    if(atomic_load(&canceled)) { mvd_sink_stop(); return -6; }
    MVDSTD_CalculateWorkBufSizeConfig sizing={0};
    sizing.level.enable=true;
    sizing.level.flag=MVD_CALC_WITH_LEVEL_FLAG_ENABLE_CALC|
        MVD_CALC_WITH_LEVEL_FLAG_ENABLE_EXTRA_OP|MVD_CALC_WITH_LEVEL_FLAG_UNK;
    sizing.level.level=(u8)level;
    sizing.width=width; sizing.height=height;
    u32 workbuf=0;
    Result result=mvdstdCalculateBufferSize(&sizing,&workbuf);

    if(result!=0 || !workbuf || workbuf>64u*1024u*1024u) { mvd_sink_stop(); return -15; }
    result=mvdstdInit(MVDMODE_VIDEOPROCESSING,MVD_INPUT_H264,MVD_OUTPUT_BGR565,workbuf,NULL);
    if(result!=0) { mvd_sink_stop(); return -4; }
    ready=true;
    mvdstdGenerateDefaultConfig(&config,stride,padded_height,stride,padded_height,NULL,NULL,NULL);
    config.physaddr_outdata0=osConvertVirtToPhys(pixels);
    memset(pixels,0,output_bytes);
    if(!cache_sync(pixels,(u32)output_bytes,0)) return -14;
    return 0;
}
void mvd_sink_cancel(void) { atomic_store(&canceled,true); }
void mvd_sink_reset_cancel(void) { atomic_store(&canceled,false); }
void mvd_sink_stop(void)
{
    if(ready) {  do { mvdstdExit(); } while(0); ready=false; }
    if(nal_allocation) { do { linearFree(nal_allocation); } while(0); nal_allocation=NULL; }
    if(pixels_allocation) { do { linearFree(pixels_allocation); } while(0); pixels_allocation=NULL; }
    output_bytes=0;
    nalbuf=NULL; pixels=NULL; width=height=stride=padded_height=0;
    validated_sps=have_sps=have_pps=drain_pending=false;

}
static void plant_corners(void)
{
    uint8_t *p=(uint8_t *)pixels;
    p[0]=0x11; p[stride*2-1]=0x11;
    p[output_bytes-stride*2]=0x11; p[output_bytes-1]=0x11;
}
static bool corners_changed(void)
{
    uint8_t *p=(uint8_t *)pixels;
    return p[0]!=0x11 || p[stride*2-1]!=0x11 ||
           p[output_bytes-stride*2]!=0x11 || p[output_bytes-1]!=0x11;
}

static int inspect_output(void)
{
    if(!cache_sync(pixels,(u32)output_bytes,3)) return -14;

    return corners_changed() ? 1 : 0;
}
static int submit_parameter(const uint8_t *data,size_t size)
{
    if(size+3+64>VIDEO_MAX_AU+64) return -5;
    nalbuf[0]=0; nalbuf[1]=0; nalbuf[2]=1;
    memcpy(nalbuf+3,data,size); memset(nalbuf+size+3,0,64);
    if(!cache_sync(nalbuf,(u32)(size+3+64),1)) return -14;

    Result result=mvdstdProcessVideoFrame(nalbuf,size+3,0,NULL);

    if(!first_nal) {  first_nal=true; }
    if(result!=MVD_STATUS_PARAMSET && result!=MVD_STATUS_FRAMEREADY && result!=MVD_STATUS_OK) return -8;
    return 0;
}
static int cache_parameter(const uint8_t *data,const VideoNal *nal,bool sps)
{
    size_t size=nal->size-nal->prefix;
    if(!size || size>256) return -5;
    uint8_t *cached=sps?sps_bytes:pps_bytes;
    size_t *cached_size=sps?&sps_size:&pps_size;
    bool *have=sps?&have_sps:&have_pps;
    const uint8_t *source=data+nal->offset+nal->prefix;
    if(*have) return *cached_size==size && !memcmp(cached,source,size) ? 0 : -9;
    if(sps) {
        VideoSps parsed;
        if(!video_parse_sps(source,size,&parsed) ||
           !video_sps_safe(&parsed,width,height,width==960 ? RP_ENV_540_MAX_MBS : RP_ENV_MAX_MBS_DEFAULT,
                           width==960 ? RP_ENV_540_MAX_DPB : RP_ENV_MAX_DPB_DEFAULT,
                            width==960 ? RP_ENV_540_MAX_LEVEL : RP_ENV_MAX_LEVEL_DEFAULT)) return -9;

        validated_sps=true;
    }
    memcpy(cached,source,size); *cached_size=size; *have=true;
    return submit_parameter(cached,size);
}
static int poll_render(void)
{

    uint32_t started=(uint32_t)chiaki_time_now_monotonic_ms();
    Result result=MVD_STATUS_BUSY;
    int wrote=0;
    for(unsigned tries=0;tries<32 && (uint32_t)chiaki_time_now_monotonic_ms()-started<250;tries++) {
        if(atomic_load(&canceled)) return -6;

        result=mvd_reference_render_poll();

        /* The reference accepts a completed output even when the control
         * result is BUSY; the pixel write is the frame-ready signal. */
        wrote=inspect_output();
        if(wrote<0) return wrote;
        if(wrote) { result=MVD_STATUS_OK; break; }
        if(result!=MVD_STATUS_BUSY) break;
        svcSleepThread(1000000);
    }
    if(!first_render) {

         first_render=true;
    }
    if(wrote<0) return wrote;
    if(result!=MVD_STATUS_OK) return result==MVD_STATUS_BUSY ? -7 : -3;
    return wrote;
}
int mvd_sink_feed_au(const uint8_t *data,size_t size,bool decode_picture)
{
    if(!ready) return -1;
    if(atomic_load(&canceled)) return -6;
    VideoAccessUnit unit;
    if(!video_parse_au(data,size,&unit)) return -5;
    if(drain_pending) {
        plant_corners();
        if(!cache_sync(pixels,(u32)output_bytes,2)) return -14;

        int wrote=poll_render();
        if(wrote<0) return wrote;
        int result=wrote ? MVD_FEED_RETRY_FRAME : MVD_FEED_RETRY_EMPTY;
        if(!wrote) drain_pending=false;

        return result;
    }
    size_t picture_size=0;
    for(unsigned index=0;index<unit.count;index++) {
        const VideoNal *nal=&unit.nals[index];
        if(nal->type==7 || nal->type==8) {
            int result=cache_parameter(data,nal,nal->type==7);
            if(result<0) return result;
        }
    }
    for(unsigned index=0;index<unit.count;index++) {
        const VideoNal *nal=&unit.nals[index];
        if(nal->type==7 || nal->type==8) continue;
        if(!unit.picture || !decode_picture) continue;
        /* AVCC-to-Annex-B in the reference emits one concatenated AU. Keep
         * non-picture NALs (SEI/AUD) in original order beside its slices. */
        size_t payload=nal->size-nal->prefix;
        if(!payload || picture_size+3+payload>VIDEO_MAX_AU) return -5;
        nalbuf[picture_size++]=0; nalbuf[picture_size++]=0; nalbuf[picture_size++]=1;
        memcpy(nalbuf+picture_size,data+nal->offset+nal->prefix,payload);
        picture_size+=payload;
    }
    if(!unit.picture || !decode_picture) return MVD_FEED_CONSUMED_EMPTY;
    if(!validated_sps || !have_pps || !picture_size) return -9;
    if(!picture_started) { picture_started=true; picture_since=(uint32_t)chiaki_time_now_monotonic_ms(); }
    memset(nalbuf+picture_size,0,64);
    if(!cache_sync(nalbuf,(u32)(picture_size+64),1)) return -14;
    plant_corners();
    if(!cache_sync(pixels,(u32)output_bytes,2)) return -14;

    Result setup=MVDSTD_SetConfig(&config);

    if(setup!=MVD_STATUS_OK && setup!=0) return -3;
    if(atomic_load(&canceled)) return -6;
    Result status=MVD_STATUS_OK;
    unsigned times=first_picture ? 2 : 1;
    for(unsigned call=0;call<times;call++) {

        status=mvdstdProcessVideoFrame(nalbuf,picture_size,0,NULL);

        /* Core2 deliberately ignores the first IDR status and judges only
         * the second submission. Recorded first IDRs can be INCOMPLETE. */
        if(call+1==times && status!=MVD_STATUS_FRAMEREADY &&
           status!=MVD_STATUS_PARAMSET) return -8;
    }
    first_picture=false;
    int wrote=inspect_output();
    if(wrote<0) return wrote;
    if(wrote) {
        drain_pending=true;
        empty_renders=0; picture_since=(uint32_t)chiaki_time_now_monotonic_ms();
        if(!first_render) {
              first_render=true; }
        return MVD_FEED_CONSUMED_FRAME;
    }
    wrote=poll_render();
    if(wrote<0) return wrote;
    if(wrote) { empty_renders=0; picture_since=(uint32_t)chiaki_time_now_monotonic_ms(); return MVD_FEED_CONSUMED_FRAME; }
    empty_renders++;

    return empty_renders<32 && (uint32_t)chiaki_time_now_monotonic_ms()-picture_since<5000 ? MVD_FEED_CONSUMED_EMPTY : -11;
}
void mvd_sink_copy_frame(uint16_t *destination)
{
    unsigned draw_width=400,draw_height=400*height/width;
    if(draw_height>240) { draw_height=240; draw_width=240*width/height; }
    unsigned left=(400-draw_width)/2,top=(240-draw_height)/2;
    memset(destination,0,400*240*sizeof(*destination));
    for(unsigned row=0;row<draw_height;row++)
        for(unsigned column=0;column<draw_width;column++)
            destination[(left+column)*240+239-top-row]=
                pixels[(row*height/draw_height)*stride+column*width/draw_width];
}

bool mvd_sink_copy_source(uint16_t *destination,size_t capacity_pixels,
                          unsigned *out_width,unsigned *out_height)
{
    if(!ready || !pixels || !destination || !out_width || !out_height ||
       !width || !height || capacity_pixels<(size_t)width*height) return false;
    for(unsigned row=0;row<height;row++)
        memcpy(destination+(size_t)row*width,pixels+(size_t)row*stride,width*sizeof(*pixels));
    *out_width=width; *out_height=height;
    return true;
}
