#ifndef THREEDSRP_MOTION_3DS_H
#define THREEDSRP_MOTION_3DS_H
#include <stdbool.h>
#include <stdint.h>
#include <chiaki/controller.h>
#include <chiaki/orientation.h>
typedef struct {
    ChiakiOrientationTracker tracker;
    bool valid;
} Motion3ds;
/* UI-thread owned preferences, applied to the next sampled motion. */
void motion_3ds_configure(bool invert_yaw, bool invert_pitch, bool invert_roll);
void motion_3ds_reset(Motion3ds *motion);
/* HID calibrated accelerometer axes; gyro arguments in physical x/y/z order,
 * i.e. libctru angularRate.x, .z (yaw), .y. Coefficient is raw units/dps. */
void motion_3ds_update(Motion3ds *motion, int ax, int ay, int az,
                       int gx, int gy, int gz, float coefficient, uint32_t us);
void motion_3ds_apply(Motion3ds *motion, ChiakiControllerState *state);
/* UI-thread owned; call after hidScanInput. Disable outside a connected session. */
void motion_3ds_aim(ChiakiControllerState *state,const ChiakiControllerState *motion,bool held,unsigned gain_q);
void motion_3ds_poll(bool connected);
void motion_3ds_apply_current(ChiakiControllerState *state);
#endif
