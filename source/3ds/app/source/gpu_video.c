#ifdef N3DS_GPU_BILINEAR
#include "gpu_video.h"
#include "texture_pack.h"
#include <citro2d.h>
#include <stdlib.h>
#include <string.h>
#include <chiaki/time.h>

enum { TRANSPARENT=1, RECT_CAPACITY=2048 };
typedef struct { uint16_t x,y,width,color; } OverlayRun;
struct GpuVideo {
    bool c3d,c2d,have_texture,have_overlay;
    C3D_Tex texture;
    C3D_RenderTarget *target;
    unsigned width,height,overlay_count;
    ZoomPresentation overlay_view;
    uint16_t overlay[400u*240u];
    OverlayRun runs[RECT_CAPACITY];
};

void gpu_video_destroy(GpuVideo *g)
{
    if(!g) return;
    /* Deleting/detaching the target drains outstanding render/transfer work.
     * C3D_FrameSync alone only waits for vblank and is NOT a GPU memory fence. */
    if(g->target) C3D_RenderTargetDelete(g->target);
    if(g->texture.data) C3D_TexDelete(&g->texture);
    if(g->c2d) C2D_Fini();
    if(g->c3d) C3D_Fini();
    free(g);
}
bool gpu_video_clear(GpuVideo *g)
{
    if(!g || !gspHasGpuRight() || !C3D_FrameBegin(0)) return false;
    C2D_TargetClear(g->target,C2D_Color32(0,0,0,255));
    C2D_SceneBegin(g->target);
    C3D_FrameEnd(0);
    return true;
}
GpuVideo *gpu_video_create(void)
{
    GpuVideo *g=calloc(1,sizeof(*g));
    if(!g) return NULL;
    if(!(g->c3d=C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))) goto fail;
    if(!(g->c2d=C2D_Init(C2D_DEFAULT_MAX_OBJECTS))) goto fail;
    C2D_Prepare();
    g->target=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    if(!g->target || !C3D_TexInit(&g->texture,1024,1024,GPU_RGB565)) goto fail;
    memset(g->texture.data,0,g->texture.size);
    C3D_TexSetFilter(&g->texture,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(&g->texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    if(!gpu_video_clear(g)) goto fail;
    return g;
fail:
    gpu_video_destroy(g);
    return NULL;
}

static bool same_view(const ZoomPresentation *a,const ZoomPresentation *b)
{
    return a->view.center_x==b->view.center_x && a->view.center_y==b->view.center_y &&
        a->view.zoom_q==b->view.zoom_q && a->box==b->box &&
        a->mode_label==b->mode_label && a->revision==b->revision;
}
static bool overlay(GpuVideo *g,unsigned width,unsigned height,const ZoomPresentation *p)
{
    if(g->have_overlay && width==g->width && height==g->height && same_view(p,&g->overlay_view)) return true;
    g->overlay_count=0;
    if(p->box || p->mode_label!=ZOOM_LABEL_NONE) {
        for(unsigned i=0;i<400u*240u;i++) g->overlay[i]=TRANSPARENT;
        if(!zoom_render_top_overlay(width,height,p,g->overlay)) return false;
        /* Reuse the exact existing bitmap badge/outline. Coalesce adjacent
         * opaque pixels into GPU rectangles; no nearest video scaling occurs. */
        for(unsigned y=0;y<240;y++) for(unsigned x=0;x<400;) {
            uint16_t color=g->overlay[x*240u+239u-y];
            if(color==TRANSPARENT) { x++; continue; }
            unsigned start=x++;
            while(x<400 && g->overlay[x*240u+239u-y]==color) x++;
            if(g->overlay_count>=RECT_CAPACITY) return false;
            g->runs[g->overlay_count++]=(OverlayRun){start,y,x-start,color};
        }
    }
    g->overlay_view=*p; g->have_overlay=true;
    return true;
}

bool gpu_video_present(GpuVideo *g,const uint16_t *source,unsigned width,unsigned height,
                       bool changed,const ZoomPresentation *p)
{
    ZoomRect crop;
    if(!g || !source || !p || !zoom_crop_rect(width,height,&p->view,&crop) ||
       !gspHasGpuRight()) return false;
    bool upload=changed || !g->have_texture || width!=g->width || height!=g->height;
    if(!overlay(g,width,height,p)) return false;
    /* FrameBegin waits for the previous queue (including the top LCD transfer)
     * before the CPU rewrites texture/vertex data used by that queue. No extra
     * 30fps limiter: the application's existing UI cadence remains in charge. */

    if(!C3D_FrameBegin(0)) {  return false; }
    if(upload) {
        if(!rgb565_texture_pack(g->texture.data,g->texture.size/2,1024,1024,
                               source,width,height,width)) {
            C3D_FrameEnd(0);  return false;
        }
        C3D_TexFlush(&g->texture);
        g->have_texture=true;
    }
    g->width=width; g->height=height;
    unsigned dw=400,dh=400*height/width;
    if(dh>240) { dh=240; dw=240*width/height; }
    Tex3DS_SubTexture sub={.width=width,.height=height,.left=0,.top=1,
        .right=(float)width/1024.0f,.bottom=1.0f-(float)height/1024.0f};
    C2D_Image picture={&g->texture,&sub};
    C2D_TargetClear(g->target,C2D_Color32(0,0,0,255));
    C2D_SceneBegin(g->target);
    bool okay=C2D_DrawImageAt(picture,(400-dw)/2,(240-dh)/2,0.5f,NULL,
                             (float)dw/width,(float)dh/height);
    for(unsigned i=0;i<g->overlay_count;i++) {
        OverlayRun *r=&g->runs[i];
        unsigned red=(r->color>>11)&31u,green=(r->color>>5)&63u,blue=r->color&31u;
        okay=C2D_DrawRectSolid(r->x,r->y,0.5f,r->width,1,
            C2D_Color32((red*255+15)/31,(green*255+31)/63,(blue*255+15)/31,255)) && okay;
    }
    C3D_FrameEnd(0);

    return okay;
}
#endif
