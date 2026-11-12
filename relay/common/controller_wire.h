#pragma once
#include "wire.h"
#include <chiaki/controller.h>
#include <math.h>
#define RELAY_CONTROL_BYTES 68u
static inline void control_float_write(uint8_t *p,float f){uint32_t bits;memcpy(&bits,&f,4);w32(p,bits);}
static inline float control_float_read(const uint8_t *p){uint32_t bits=r32(p);float f;memcpy(&f,&bits,4);return f;}
static inline void relay_control_encode(uint8_t *p,const ChiakiControllerState *s){
 memset(p,0,RELAY_CONTROL_BYTES);w32(p,s->buttons);w16(p+4,s->left_x);w16(p+6,s->left_y);w16(p+8,s->right_x);w16(p+10,s->right_y);p[12]=s->l2_state;p[13]=s->r2_state;p[14]=s->touch_id_next;p[15]=1;
 for(unsigned i=0;i<2;i++){unsigned off=16+6*i;p[off]=(uint8_t)s->touches[i].id;w16(p+off+2,s->touches[i].x);w16(p+off+4,s->touches[i].y);}
 float f[]={s->gyro_x,s->gyro_y,s->gyro_z,s->accel_x,s->accel_y,s->accel_z,s->orient_x,s->orient_y,s->orient_z,s->orient_w};for(unsigned i=0;i<10;i++)control_float_write(p+28+4*i,f[i]);
}
static inline bool relay_control_decode(const uint8_t *p,size_t n,ChiakiControllerState *s){
 if(n!=RELAY_CONTROL_BYTES||p[15]!=1||(r32(p)&~0x3ffffu)||p[14]>127)return false;
 chiaki_controller_state_set_idle(s);s->buttons=r32(p);s->left_x=(int16_t)r16(p+4);s->left_y=(int16_t)r16(p+6);s->right_x=(int16_t)r16(p+8);s->right_y=(int16_t)r16(p+10);s->l2_state=p[12];s->r2_state=p[13];s->touch_id_next=p[14];
 for(unsigned i=0;i<2;i++){unsigned off=16+6*i;if(p[off]!=255&&p[off]>127)return false;s->touches[i].id=(int8_t)p[off];s->touches[i].x=r16(p+off+2);s->touches[i].y=r16(p+off+4);if(s->touches[i].id>=0&&(s->touches[i].x>1919||s->touches[i].y>1079))return false;}
 float f[10];for(unsigned i=0;i<10;i++){f[i]=control_float_read(p+28+4*i);if(!isfinite(f[i])||fabsf(f[i])>(i<3?30.f:i<6?5.f:1.01f))return false;}
 float norm=f[6]*f[6]+f[7]*f[7]+f[8]*f[8]+f[9]*f[9];if(norm<0.9f||norm>1.1f)return false;
 s->gyro_x=f[0];s->gyro_y=f[1];s->gyro_z=f[2];s->accel_x=f[3];s->accel_y=f[4];s->accel_z=f[5];s->orient_x=f[6];s->orient_y=f[7];s->orient_z=f[8];s->orient_w=f[9];return true;
}
