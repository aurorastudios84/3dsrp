#include "zoom_controls.h"
#include <string.h>

static ZoomView default_view(void)
{
    return (ZoomView){ZOOM_CENTER,ZOOM_CENTER,ZOOM_DEFAULT_Q};
}
bool zoom_controls_menu(const ZoomControls *c)
{
    return c->mode==ZOOM_READING || c->mode==ZOOM_HUD_EDIT || c->mode==ZOOM_DIRTY;
}
bool zoom_controls_dirty(const ZoomControls *c)
{
    return c->draft.center_x!=c->saved.center_x || c->draft.center_y!=c->saved.center_y ||
        c->draft.zoom_q!=c->saved.zoom_q;
}
void zoom_controls_rearm(ZoomControls *c)
{
    c->mode=ZOOM_REARM; c->revision++;
}
void zoom_controls_enter(ZoomControls *c,uint32_t held,uint32_t now)
{
    c->mode=ZOOM_READING;
    c->draft=c->saved; c->draft_committed=true; c->help_visible=true;
    c->previous_held=held; c->last_ms=now; c->revision++;
}
void zoom_controls_init(ZoomControls *c)
{
    memset(c,0,sizeof(*c));
    c->reading=c->draft=c->saved=default_view();
    c->help_visible=true;
}
void zoom_controls_set_views(ZoomControls *c,const ZoomView *reading,const ZoomView *hud)
{
    if(!c || !reading || !hud) return;
    c->reading=*reading;
    c->saved=*hud;
    c->draft=*hud;
}
void zoom_controls_reset_session(ZoomControls *c)
{
    c->mode=ZOOM_GAMEPLAY; c->previous_held=0; c->last_ms=0;
    c->previous_touch=false;
    c->draft=c->saved; c->draft_committed=true; c->revision++;
}
ZoomPresentation zoom_controls_presentation(const ZoomControls *c)
{
    ZoomView v=(c->mode==ZOOM_READING || (c->mode==ZOOM_DIRTY && c->dirty_origin==ZOOM_READING)) ? c->reading :
        (c->mode==ZOOM_HUD_EDIT || c->mode==ZOOM_DIRTY) ? c->draft : c->saved;
    return (ZoomPresentation){.view=v,.box=zoom_controls_menu(c),.revision=c->revision,
        .mode_label=c->mode==ZOOM_READING ? ZOOM_LABEL_READING :
            c->mode==ZOOM_HUD_EDIT ? ZOOM_LABEL_HUD : ZOOM_LABEL_NONE};
}
static int magnitude(int n) { return n<0 ? -n : n; }
static bool released(const Input3dsSnapshot *s)
{
    return !s->held && !s->touch_down &&
        magnitude(s->circle_x)<=INPUT_3DS_AXIS_DEADZONE &&
        magnitude(s->circle_y)<=INPUT_3DS_AXIS_DEADZONE &&
        magnitude(s->cstick_x)<=INPUT_3DS_AXIS_DEADZONE &&
        magnitude(s->cstick_y)<=INPUT_3DS_AXIS_DEADZONE;
}
static uint16_t move_axis(uint16_t position,int raw,uint32_t ms)
{
    int amount=magnitude(raw)-INPUT_3DS_AXIS_DEADZONE;
    if(amount<=0) return position;
    if(amount>INPUT_3DS_AXIS_MAX-INPUT_3DS_AXIS_DEADZONE)
        amount=INPUT_3DS_AXIS_MAX-INPUT_3DS_AXIS_DEADZONE;
    int32_t delta=(int32_t)((int64_t)amount*32768*ms/
        ((INPUT_3DS_AXIS_MAX-INPUT_3DS_AXIS_DEADZONE)*1000));
    int32_t value=(int32_t)position+(raw<0 ? -delta : delta);
    return (uint16_t)(value<0 ? 0 : value>65535 ? 65535 : value);
}
static void clamp_view(ZoomView *v,unsigned width,unsigned height)
{
    ZoomRect rect;
    if(!zoom_crop_rect(width,height,v,&rect)) return;
    /* Normalized pixel centre, with ceil at the low edge and floor at high.
     * Keep movement responsive immediately when reversing at an image edge. */
    uint32_t min_x=(rect.width*65535u+2u*width-1u)/(2u*width);
    uint32_t min_y=(rect.height*65535u+2u*height-1u)/(2u*height);
    uint32_t max_x=((2u*width-rect.width)*65535u)/(2u*width);
    uint32_t max_y=((2u*height-rect.height)*65535u)/(2u*height);
    if(v->center_x<min_x) v->center_x=(uint16_t)min_x;
    if(v->center_x>max_x) v->center_x=(uint16_t)max_x;
    if(v->center_y<min_y) v->center_y=(uint16_t)min_y;
    if(v->center_y>max_y) v->center_y=(uint16_t)max_y;
}
ZoomControlResult zoom_controls_update(ZoomControls *c,
    const Input3dsSnapshot *s,uint32_t now,unsigned width,unsigned height)
{
    ZoomControlResult result={0};
    ZoomMode before=c->mode;
    uint32_t pressed=s->held & ~c->previous_held;
    bool touch_pressed=s->touch_down && !c->previous_touch;
    uint32_t elapsed=now-c->last_ms;
    if(elapsed>50) elapsed=50;
    c->last_ms=now; c->previous_held=s->held; c->previous_touch=s->touch_down;
    if(!width || !height) { width=640; height=360; }
    result.consume_start=c->mode!=ZOOM_GAMEPLAY;
    if(c->mode==ZOOM_GAMEPLAY) {
        if(pressed & RP_INPUT_START) {
            zoom_controls_rearm(c); result.open_session=true;
            result.reset_touch=result.consume_start=true;
        } else if(pressed & RP_INPUT_SELECT) {
            zoom_controls_enter(c,s->held,now);
            result.entered=result.reset_touch=result.consume_start=true;
        } else result.forward=true;
    } else if(c->mode==ZOOM_DIRTY) {
        unsigned old_row=c->dirty_row;
        if(pressed & RP_INPUT_DPAD_UP) c->dirty_row=(c->dirty_row+2)%3;
        else if(pressed & RP_INPUT_DPAD_DOWN) c->dirty_row=(c->dirty_row+1)%3;
        else if(pressed & RP_INPUT_B) c->mode=c->dirty_origin;
        else if(pressed & RP_INPUT_A) {
            if(c->dirty_row==2) c->mode=c->dirty_origin;
            else {
                if(c->dirty_row==0) {
                    c->saved=c->draft; c->saves++; result.saved=true;
                } else c->draft=c->saved;
                c->draft_committed=true;
                zoom_controls_rearm(c); result.reset_touch=true;
                result.open_session=c->dirty_to_session;
            }
        }
        if(old_row!=c->dirty_row) c->revision++;
    } else if(zoom_controls_menu(c)) {
        if(pressed & RP_INPUT_START) {
            zoom_controls_rearm(c); result.open_session=true;
            result.reset_touch=true;
        } else if(pressed & RP_INPUT_SELECT) {
            if(c->mode==ZOOM_READING) c->mode=ZOOM_HUD_EDIT;
            else zoom_controls_rearm(c);
            result.reset_touch=true;
        } else if(pressed & RP_INPUT_B) {
            zoom_controls_rearm(c); result.reset_touch=true;
        } else {
            if(touch_pressed && s->touch_x<152 && s->touch_y>=40 && s->touch_y<48)
                result.open_help=true;
            ZoomView *v=c->mode==ZOOM_READING ? &c->reading : &c->draft;
            ZoomView old=*v;
            v->center_x=move_axis(v->center_x,s->circle_x,elapsed);
            v->center_y=move_axis(v->center_y,-(int)s->circle_y,elapsed);
            if((pressed & RP_INPUT_L) && !(s->held & RP_INPUT_R) && v->zoom_q>ZOOM_MIN_Q)
                v->zoom_q-=2;
            if((pressed & RP_INPUT_R) && !(s->held & RP_INPUT_L) && v->zoom_q<ZOOM_MAX_Q)
                v->zoom_q+=2;
            if(pressed & RP_INPUT_Y) v->center_x=v->center_y=ZOOM_CENTER;
            clamp_view(v,width,height);
            if(old.center_x!=v->center_x || old.center_y!=v->center_y || old.zoom_q!=v->zoom_q) {
                c->revision++;
                result.saved=true;
                if(c->mode==ZOOM_HUD_EDIT) {
                    c->saved=c->draft;
                    c->draft_committed=true;
                    c->saves++;
                }
            }
        }
    } else {
        if(released(s)) { c->mode=ZOOM_GAMEPLAY; result.reset_touch=true; }
        /* The neutral sample itself is never forwarded. */
    }
    result.mode_changed=before!=c->mode;
    if(result.mode_changed) c->revision++;
    return result;
}
