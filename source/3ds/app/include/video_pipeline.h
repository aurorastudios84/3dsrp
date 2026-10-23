#ifndef N3DS_VIDEO_PIPELINE_H
#define N3DS_VIDEO_PIPELINE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "zoom.h"
#define VIDEO_QUEUE_CAPACITY 4u
#define VIDEO_SCREEN_PIXELS (400u*240u)
typedef struct VideoPipeline VideoPipeline;
typedef struct {
    uint32_t received, decoded, presented, dropped, errors, queue_depth;
    unsigned width, height;
    bool running, failed;
} VideoStats;
VideoPipeline *video_pipeline_create(void);
bool video_pipeline_start(VideoPipeline *pipeline);
bool video_pipeline_push(VideoPipeline *pipeline, const uint8_t *data, size_t size,
                          unsigned width, unsigned height, int32_t lost);
void video_pipeline_stop(VideoPipeline *pipeline);
void video_pipeline_quiesce(VideoPipeline *pipeline);
bool video_pipeline_idle(VideoPipeline *pipeline);
bool video_pipeline_join(VideoPipeline *pipeline);
bool video_pipeline_present(VideoPipeline *pipeline, uint16_t *framebuffer);
bool video_pipeline_present_zoom(VideoPipeline *pipeline,uint16_t *top,uint16_t *bottom,
                                 const ZoomPresentation *presentation);
VideoStats video_pipeline_stats(VideoPipeline *pipeline);
void video_pipeline_destroy(VideoPipeline *pipeline);
/* UI-owned clear after a completed disconnect; never called during teardown. */
bool video_pipeline_clear_top(VideoPipeline *pipeline);
/* UI-thread-only artwork in Ready/Disconnected; no video frames consumed. */
bool video_pipeline_present_ui(VideoPipeline *pipeline,const uint16_t *rgb565);
#endif
