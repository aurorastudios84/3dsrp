#include "zoom.h"
#include <string.h>

static bool geometry(unsigned width,unsigned height)
{ return width>=4 && height>=3 && width<=960 && height<=540; }
static unsigned clamp_origin(unsigned center,unsigned extent,unsigned bound)
{
    unsigned pixel=(unsigned)(((uint64_t)center*bound+32767u)/65535u);
    unsigned origin=pixel>extent/2 ? pixel-extent/2 : 0;
    return origin>bound-extent ? bound-extent : origin;
}
bool zoom_crop_rect(unsigned width,unsigned height,const ZoomView *view,ZoomRect *crop)
{
    if(!view || !crop || !geometry(width,height) || view->zoom_q<ZOOM_MIN_Q ||
       view->zoom_q>ZOOM_MAX_Q || (view->zoom_q&1u)) return false;
    /* One unit is four by three source pixels. Magnification is relative to
     * aspect-fit top screen scale, not relative to the entire source width. */
    unsigned units=width*240u>=height*400u ?
        (width*320u)/(400u*view->zoom_q) :
        (height*320u)/(240u*view->zoom_q);
    if(!units) units=1;
    if(units>width/4) units=width/4;
    if(units>height/3) units=height/3;
    crop->width=units*4; crop->height=units*3;
    crop->x=clamp_origin(view->center_x,crop->width,width);
    crop->y=clamp_origin(view->center_y,crop->height,height);
    return true;
}
static void sample(const uint16_t *source,unsigned stride,const ZoomRect *crop,
                   uint16_t *output,unsigned screen_width,unsigned draw_width,
                   unsigned draw_height,unsigned left,unsigned top)
{
    unsigned xs[400],ys[240];
    for(unsigned x=0;x<draw_width;x++) xs[x]=crop->x+x*crop->width/draw_width;
    for(unsigned y=0;y<draw_height;y++) ys[y]=(crop->y+y*crop->height/draw_height)*stride;
    memset(output,0,screen_width*240u*sizeof(*output));
    for(unsigned x=0;x<draw_width;x++) {
        uint16_t *column=output+(left+x)*240u+239u-top;
        for(unsigned y=0;y<draw_height;y++) column[-(int)y]=source[ys[y]+xs[x]];
    }
}
static uint16_t label_color(ZoomLabel label)
{
    return label==ZOOM_LABEL_READING ? 0x07ffu :
        label==ZOOM_LABEL_HUD ? 0xfd20u : 0xffffu;
}
/* Explicit 5x7 uppercase rows, high bit at the left. Only the letters in
 * READING and HUD SETUP are needed; no external font/runtime dependencies. */
static const uint8_t *glyph(char letter)
{
    static const char letters[]="RADEINGHUSTP";
    static const uint8_t rows[][7]={
        {30,17,17,30,20,18,17}, /* R */
        {14,17,17,31,17,17,17}, /* A */
        {30,17,17,17,17,17,30}, /* D */
        {31,16,16,30,16,16,31}, /* E */
        {31,4,4,4,4,4,31},     /* I */
        {17,25,25,21,19,19,17}, /* N */
        {14,17,16,23,17,17,15}, /* G */
        {17,17,17,31,17,17,17}, /* H */
        {17,17,17,17,17,17,14}, /* U */
        {15,16,16,14,1,1,30},   /* S */
        {31,4,4,4,4,4,4},       /* T */
        {30,17,17,30,16,16,16}  /* P */
    };
    for(unsigned i=0;i<sizeof(letters)-1;i++) if(letters[i]==letter) return rows[i];
    return NULL;
}
static void mode_badge(uint16_t *top,ZoomLabel label)
{
    const char *text=label==ZOOM_LABEL_READING ? "READING" :
        label==ZOOM_LABEL_HUD ? "HUD SETUP" : NULL;
    if(!text) return;
    unsigned length=(unsigned)strlen(text),width=length*6u+4u,left=(400u-width)/2u;
    uint16_t color=label_color(label);
    /* Nine-pixel black backing remains visible over source pixels and the
     * crop outline. This is rendered privately before pipeline publication. */
    for(unsigned x=left;x<left+width;x++) for(unsigned y=0;y<9;y++)
        top[x*240u+239u-y]=0;
    for(unsigned i=0;i<length;i++) {
        const uint8_t *rows=glyph(text[i]);
        if(!rows) continue;
        for(unsigned y=0;y<7;y++) for(unsigned x=0;x<5;x++)
            if(rows[y] & (1u<<(4u-x))) top[(left+2u+i*6u+x)*240u+238u-y]=color;
    }
}
static void border(uint16_t *top,unsigned x0,unsigned y0,unsigned x1,unsigned y1,uint16_t color)
{
    for(unsigned x=x0;x<=x1;x++) {
        top[x*240+239-y0]=color; top[x*240+239-y1]=color;
        if(y0+1<y1) { top[x*240+238-y0]=0; top[x*240+240-y1]=0; }
    }
    for(unsigned y=y0;y<=y1;y++) {
        top[x0*240+239-y]=color; top[x1*240+239-y]=color;
        if(x0+1<x1) { top[(x0+1)*240+239-y]=0; top[(x1-1)*240+239-y]=0; }
    }
}
bool zoom_render_top_overlay(unsigned width,unsigned height,
                             const ZoomPresentation *presentation,uint16_t *top)
{
    ZoomRect crop;
    if(!top || !zoom_crop_rect(width,height,presentation ? &presentation->view : NULL,&crop)) return false;
    unsigned dw=400,dh=400*height/width;
    if(dh>240) { dh=240; dw=240*width/height; }
    if(!dw || !dh) return false;
    unsigned left=(400-dw)/2,upper=(240-dh)/2;
    if(presentation->box) {
        unsigned x0=left+crop.x*dw/width,y0=upper+crop.y*dh/height;
        unsigned x1=left+(crop.x+crop.width-1)*dw/width;
        unsigned y1=upper+(crop.y+crop.height-1)*dh/height;
        border(top,x0,y0,x1,y1,label_color(presentation->mode_label));
    }
    mode_badge(top,presentation->mode_label);
    return true;
}
bool zoom_render_frame(const uint16_t *source,unsigned width,unsigned height,
                       unsigned stride,const ZoomPresentation *presentation,
                       uint16_t *top,uint16_t *bottom)
{
    ZoomRect crop;
    if(!source || (!top && !bottom) || stride<width || stride>960 ||
       !zoom_crop_rect(width,height,presentation ? &presentation->view : NULL,&crop)) return false;
    unsigned dw=400,dh=400*height/width;
    if(dh>240) { dh=240; dw=240*width/height; }
    if(!dw || !dh) return false;
    unsigned left=(400-dw)/2,upper=(240-dh)/2;
    if(top) {
        ZoomRect full={0,0,width,height};
        sample(source,stride,&full,top,400,dw,dh,left,upper);
        zoom_render_top_overlay(width,height,presentation,top);
    }
    if(bottom) sample(source,stride,&crop,bottom,320,320,240,0,0);
    return true;
}
