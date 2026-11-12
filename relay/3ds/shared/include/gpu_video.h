#ifndef N3DS_GPU_VIDEO_H
#define N3DS_GPU_VIDEO_H
#include "zoom.h"
typedef struct GpuVideo GpuVideo;
/* All functions are UI-thread-only. Only GFX_TOP is linked to Citro2D.
 * LCD top format must be BGR8 with double buffering; bottom remains CPU-owned. */
GpuVideo *gpu_video_create(void);
bool gpu_video_present(GpuVideo *gpu,const uint16_t *source,unsigned width,unsigned height,
                       bool changed,const ZoomPresentation *presentation);
bool gpu_video_clear(GpuVideo *gpu);
void gpu_video_destroy(GpuVideo *gpu);
#endif
