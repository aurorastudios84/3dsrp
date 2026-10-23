#ifndef N3DS_ZOOM_CONTROLS_H
#define N3DS_ZOOM_CONTROLS_H
#include "zoom.h"
#include "input_3ds.h"
typedef enum { ZOOM_GAMEPLAY, ZOOM_READING, ZOOM_HUD_EDIT, ZOOM_REARM, ZOOM_DIRTY } ZoomMode;
typedef struct {
    ZoomMode mode;
    ZoomView reading, draft, saved;
    uint32_t previous_held, last_ms, revision, saves;
    bool draft_committed, help_visible;
    bool previous_touch, dirty_to_session;
    ZoomMode dirty_origin;
    unsigned dirty_row;
} ZoomControls;
typedef struct {
    bool forward, reset_touch, consume_start, entered, saved, mode_changed;
    bool open_session, open_help;
} ZoomControlResult;
void zoom_controls_init(ZoomControls *controls);
void zoom_controls_set_views(ZoomControls *controls,const ZoomView *reading,const ZoomView *hud);
/* End a connection while retaining reading zoom and the session-lifetime HUD. */
void zoom_controls_reset_session(ZoomControls *controls);
void zoom_controls_enter(ZoomControls *controls,uint32_t held,uint32_t now_ms);
void zoom_controls_rearm(ZoomControls *controls);
bool zoom_controls_dirty(const ZoomControls *controls);
bool zoom_controls_menu(const ZoomControls *controls);
ZoomPresentation zoom_controls_presentation(const ZoomControls *controls);
ZoomControlResult zoom_controls_update(ZoomControls *controls,
    const Input3dsSnapshot *snapshot, uint32_t now_ms, unsigned width, unsigned height);
#endif
