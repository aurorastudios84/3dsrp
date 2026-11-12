#pragma once
#include "input_3ds.h"
typedef struct {
 Input3dsTouchCapture touch;
 unsigned page,last_surface,revision,gesture;
 bool usage_allowed,usage_failed;unsigned usage_decision,usage_selection,usage_return_page;uint32_t usage_keys;
 bool gyro_aim;unsigned aim_gain_q;
 bool select_down,extra_down,suppress,motion,yaw,pitch,roll,magnify,adjust;
 uint32_t extra_button;bool stick;int origin_x,origin_y;
 unsigned crop_x,crop_y,zoom_q;
} RelayControls;
void relay_controls_map(RelayControls *,const Input3dsSnapshot *,ChiakiControllerState *);
void relay_controls_sample(RelayControls *,unsigned,bool,ChiakiControllerState *);
void relay_controls_draw(const RelayControls *);
void relay_controls_init(RelayControls *);
void relay_controls_save(const RelayControls *);

void relay_controls_set_console(void *console);
void relay_controls_animate(const RelayControls *,uint32_t now);
void relay_controls_present(void);

void relay_connection_draw(uint16_t *pixels,unsigned view,unsigned source);
