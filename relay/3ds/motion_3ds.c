#include "motion_3ds.h"
#include <math.h>
#ifdef __3DS__
#include <3ds.h>
#include <chiaki/time.h>
#endif

static bool invert_yaw, invert_pitch, invert_roll;
void motion_3ds_configure(bool yaw,bool pitch,bool roll)
{
    invert_yaw=yaw; invert_pitch=pitch; invert_roll=roll;
}
static float limit(float v, float bound) { return fmaxf(-bound,fminf(bound,v)); }
void motion_3ds_reset(Motion3ds *m)
{
    chiaki_orientation_tracker_init(&m->tracker);
    m->valid=false;
}
void motion_3ds_update(Motion3ds *m,int ax,int ay,int az,int gx,int gy,int gz,
                       float coefficient,uint32_t us)
{
    if(!isfinite(coefficient) || coefficient<=0 || (ax==0 && ay==0 && az==0 && gx==0 && gy==0 && gz==0)) {
        motion_3ds_reset(m); return;
    }
    /* HID accelerometer reports gravity, 512 units/g. Chiaki expects specific
     * force: +Y at rest. Gyro coefficient is a divisor, not a multiplier. */
    /* Mirror lateral motion to match the PS5 controller frame. Acceleration
     * is a polar vector: reflect X. Angular velocity is an axial vector:
     * reflect Y/Z together, preserving pitch and consistent sensor fusion. */
    float a[3]={limit(ax/512.f,5),limit(-ay/512.f,5),limit(-az/512.f,5)};
    float scale=0.017453292519943295f/coefficient;
    float g[3]={limit(gx*scale,30),limit(-gy*scale,30),limit(-gz*scale,30)};
    /* Never integrate a suspend/stall as if the latest angular velocity had
     * persisted throughout it. Unsigned subtraction also handles timer wrap. */
    if(m->valid && (uint32_t)(us-m->tracker.timestamp)>100000u)
        motion_3ds_reset(m);
    if(m->valid && us==m->tracker.timestamp) return;
    if(!m->valid) {
        /* Seed the IMU from measured gravity instead of exposing Chiaki's
         * beta=20 startup at handheld sample rates. At 15 Hz that correction
         * overshoots by radians and can leave the pose on the wrong side. */
        float length=sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
        if(!isfinite(length) || length<0.25f) return;
        float x=a[0]/length,y=a[1]/length,z=a[2]/length;
        float qx=y,qy=-x,qw=1.0f+z;
        float norm=sqrtf(qx*qx+qy*qy+qw*qw);
        if(norm<0.00001f) {
            m->tracker.orient=(ChiakiOrientation){.x=1,.y=0,.z=0,.w=0};
        } else {
            m->tracker.orient=(ChiakiOrientation){.x=qx/norm,.y=qy/norm,.z=0,.w=qw/norm};
        }
        /* Preserve the tracker's normal fusion and basis conversion, but
         * skip its high-gain warmup; the first integration interval is zero. */
        m->tracker.sample_index=30;
        m->tracker.timestamp=us;
    }
    ChiakiAccelNewZero zero={0};
    chiaki_orientation_tracker_update(&m->tracker,g[0],g[1],g[2],a[0],a[1],a[2],&zero,true,us);
    m->valid=true;
}
void motion_3ds_apply(Motion3ds *m,ChiakiControllerState *state)
{
    if(!m->valid) return;
    chiaki_orientation_tracker_apply_to_controller_state(&m->tracker,state);
    float norm=sqrtf(state->orient_x*state->orient_x+state->orient_y*state->orient_y+
                     state->orient_z*state->orient_z+state->orient_w*state->orient_w);
    if(!isfinite(norm) || norm<0.001f) {
        motion_3ds_reset(m);
        state->orient_x=state->orient_y=state->orient_z=0;state->orient_w=1;
    } else {
        state->orient_x/=norm;state->orient_y/=norm;state->orient_z/=norm;state->orient_w/=norm;
    }
    /* Forward/back correction in the final controller frame. Reflect polar
     * acceleration Z, axial gyro X/Y and quaternion X/Y together. Applying
     * this after fusion also preserves the tracker's existing filtering and
     * left/right response exactly (its component-wise fuzz is not invariant
     * under the internal 90-degree orientation basis conversion). */
    state->accel_z=-state->accel_z;
    state->gyro_x=-state->gyro_x;
    state->gyro_y=-state->gyro_y;
    state->orient_x=-state->orient_x;
    state->orient_y=-state->orient_y;
    /* Fixed 1x sensitivity: forward calibrated sensor values without gain.
     * Invert rotational axes independently in the final controller frame:
     * pitch=X, yaw=Y, roll=Z. Quaternion vector components encode the rotation
     * axis; changing their signs preserves angle and unit length. */
    if(invert_yaw) {
        state->accel_x=-state->accel_x;
        state->gyro_y=-state->gyro_y;
        state->orient_y=-state->orient_y;
    }
    if(invert_pitch) {
        state->accel_z=-state->accel_z;
        state->gyro_x=-state->gyro_x;
        state->orient_x=-state->orient_x;
    }
    if(invert_roll) {
        state->gyro_z=-state->gyro_z;
        state->orient_z=-state->orient_z;
    }
}
/* Turn-rate aiming; no integrated tilt or centre position. */
static int aim_axis(float rate,unsigned gain_q){
    if(!isfinite(rate)||fabsf(rate)<=0.025f)return 0;
    float speed=(fabsf(rate)-0.025f)*(gain_q/4.0f)/1.5707963f;
    /* Clear typical game stick deadzones without amplifying sensor noise. */
    float value=fminf(1.0f,0.18f+0.82f*speed)*32767.0f;
    return rate<0?-(int)value:(int)value;
}
void motion_3ds_aim(ChiakiControllerState *state,const ChiakiControllerState *motion,bool held,unsigned gain_q){
    if(!held)return;
    if(gain_q<1)gain_q=1;
    if(gain_q>16)gain_q=16;
    int x=state->right_x+aim_axis(-motion->gyro_y,gain_q);
    int y=state->right_y+aim_axis(-motion->gyro_x,gain_q);
    state->right_x=x< -32767?-32767:x>32767?32767:x;
    state->right_y=y< -32767?-32767:y>32767?32767:y;
}
#ifdef __3DS__
static Motion3ds current;
static bool attempted,accel_enabled,gyro_enabled;
static float gyro_coefficient;
static uint32_t last_sample_us;
static bool sample_clock_valid;
static ChiakiControllerState cached_motion;
#define MOTION_PERIOD_US 33333u
void motion_3ds_poll(bool connected)
{
    if(!connected) {
        if(gyro_enabled) HIDUSER_DisableGyroscope();
        if(accel_enabled) HIDUSER_DisableAccelerometer();
        attempted=accel_enabled=gyro_enabled=false;
        sample_clock_valid=false;
        motion_3ds_reset(&current);
        return;
    }
    if(!attempted) {
        attempted=true;
        motion_3ds_reset(&current);
        accel_enabled=R_SUCCEEDED(HIDUSER_EnableAccelerometer());
        gyro_enabled=R_SUCCEEDED(HIDUSER_EnableGyroscope());
        if(R_FAILED(HIDUSER_GetGyroscopeRawToDpsCoefficient(&gyro_coefficient))) gyro_coefficient=0;
        /* Leave ordinary controls usable if either sensor is unavailable. */
        if(!accel_enabled || !gyro_enabled || !isfinite(gyro_coefficient) || gyro_coefficient<=0) {
            if(gyro_enabled) HIDUSER_DisableGyroscope();
            if(accel_enabled) HIDUSER_DisableAccelerometer();
            accel_enabled=gyro_enabled=false;
        }
        return; /* current HID scan predates sensor enable */
    }
    if(!accel_enabled || !gyro_enabled) return;
    uint32_t now=(uint32_t)chiaki_time_now_monotonic_us();
    if(sample_clock_valid && (uint32_t)(now-last_sample_us)<MOTION_PERIOD_US) return;
    /* No catch-up burst after a delayed frame. Buttons/sticks use the latest
     * cached motion without waiting for the next sensor deadline. */
    last_sample_us=now;sample_clock_valid=true;
    accelVector a; angularRate g;
    hidAccelRead(&a);hidGyroRead(&g);
    motion_3ds_update(&current,a.x,a.y,a.z,g.x,g.z,g.y,gyro_coefficient,
                      now);
    chiaki_controller_state_set_idle(&cached_motion);
    motion_3ds_apply(&current,&cached_motion);
}
void motion_3ds_apply_current(ChiakiControllerState *state)
{
    if(!current.valid) return;
    state->accel_x=cached_motion.accel_x;state->accel_y=cached_motion.accel_y;state->accel_z=cached_motion.accel_z;
    state->gyro_x=cached_motion.gyro_x;state->gyro_y=cached_motion.gyro_y;state->gyro_z=cached_motion.gyro_z;
    state->orient_x=cached_motion.orient_x;state->orient_y=cached_motion.orient_y;
    state->orient_z=cached_motion.orient_z;state->orient_w=cached_motion.orient_w;
}
#endif
