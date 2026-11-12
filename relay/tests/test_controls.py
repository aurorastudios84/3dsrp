#!/usr/bin/env python3
"""Shared main-app mapping, original-3DS fallbacks, motion and full-state wire validation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];app=root/'3ds/shared';core=root/'vendor/chiaki-upstream'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.c').write_text(r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include "controls.h"
#include "controller_wire.h"
#include "motion_3ds.h"
static void touch(RelayControls *u,Input3dsSnapshot *s,ChiakiControllerState *c,int x,int y){s->touch_down=false;relay_controls_map(u,s,c);s->touch_down=true;s->touch_x=x;s->touch_y=y;relay_controls_map(u,s,c);}
int main(void){
 RelayControls u={0};Input3dsSnapshot s={0};ChiakiControllerState c,d;input_3ds_reset_capture(&u.touch);
 s.held=RP_INPUT_A|RP_INPUT_B|RP_INPUT_X|RP_INPUT_Y|RP_INPUT_L|RP_INPUT_R|RP_INPUT_ZL|RP_INPUT_ZR|RP_INPUT_DPAD_UP|RP_INPUT_DPAD_DOWN|RP_INPUT_DPAD_LEFT|RP_INPUT_DPAD_RIGHT;
 s.circle_x=156;s.circle_y=-156;s.cstick_x=-156;s.cstick_y=156;relay_controls_map(&u,&s,&c);
 assert((c.buttons&0x3ff)==0x3ff&&c.l2_state==255&&c.r2_state==255&&c.left_x==32767&&c.left_y==32767&&c.right_x==-32767&&c.right_y==-32767);
 s=(Input3dsSnapshot){0};s.held=RP_INPUT_B;relay_controls_map(&u,&s,&c);assert(c.buttons==CHIAKI_CONTROLLER_BUTTON_CROSS);s.held=RP_INPUT_A;relay_controls_map(&u,&s,&c);assert(c.buttons==CHIAKI_CONTROLLER_BUTTON_MOON);s.held=0;
 u.zoom_q=8;u.crop_x=u.crop_y=32768;
 // Physical inputs remain live on every page and while magnified.
 for(unsigned page=0;page<4;page++){u.page=page;s.held=RP_INPUT_A|RP_INPUT_L;s.circle_x=156;relay_controls_map(&u,&s,&c);assert(c.buttons&CHIAKI_CONTROLLER_BUTTON_MOON);assert(c.l2_state==255&&c.left_x==32767);}
 u.page=0;s=(Input3dsSnapshot){0};
 touch(&u,&s,&c,20,50);assert(c.buttons==CHIAKI_CONTROLLER_BUTTON_L1);
 touch(&u,&s,&c,90,50);assert(c.buttons==CHIAKI_CONTROLLER_BUTTON_L3);
 touch(&u,&s,&c,120,50);assert(c.buttons==CHIAKI_CONTROLLER_BUTTON_R3);
 touch(&u,&s,&c,165,50);assert(c.buttons==CHIAKI_CONTROLLER_BUTTON_R1);
 touch(&u,&s,&c,220,50);assert(c.r2_state==255&&c.right_x==0&&c.right_y==0);
 s.touch_x=160;s.touch_y=120;relay_controls_map(&u,&s,&c);assert(c.r2_state==255&&c.right_x==-32767&&c.right_y==32767); // fire and aim remain captured
 s.touch_x=250;s.touch_y=20;relay_controls_map(&u,&s,&c);assert(c.r2_state==255&&c.right_x>0&&c.right_y<0&&u.page==0);
 s.touch_down=false;relay_controls_map(&u,&s,&c);assert(c.r2_state==0&&c.right_x==0&&c.right_y==0);
 s.held=RP_INPUT_R;relay_controls_map(&u,&s,&c);assert(c.r2_state==255);s.held=0;
 touch(&u,&s,&c,280,50);assert(c.buttons==CHIAKI_CONTROLLER_BUTTON_OPTIONS);
 touch(&u,&s,&c,160,120);s.touch_x=220;s.touch_y=60;relay_controls_map(&u,&s,&c);assert(c.right_x==32767&&c.right_y==-32767);
 // A captured stick drag crossing the toolbar cannot toggle magnification.
 s.touch_x=30;s.touch_y=220;relay_controls_map(&u,&s,&c);assert(!u.magnify&&c.right_x==-32767);
 touch(&u,&s,&c,20,220);assert(u.magnify);
 s.touch_x=160;s.touch_y=120;relay_controls_map(&u,&s,&c);assert(c.right_x==0); // toolbar gesture stays local
 touch(&u,&s,&c,190,220);assert(u.zoom_q==10);
 touch(&u,&s,&c,100,220);assert(u.zoom_q==8);
 touch(&u,&s,&c,260,220);assert(u.adjust);
 touch(&u,&s,&c,100,120);s.touch_x=120;s.touch_y=130;relay_controls_map(&u,&s,&c);assert(u.crop_x==32768+5120&&u.crop_y==32768+2560&&c.right_x==0);
 touch(&u,&s,&c,250,90);assert(u.crop_x==32768&&u.crop_y==32768);
 touch(&u,&s,&c,260,220);assert(!u.adjust);
 touch(&u,&s,&c,160,15);assert(u.page==1);
 touch(&u,&s,&c,160,120);assert(c.touches[0].id>=0);
 int start_x=c.touches[0].x,start_y=c.touches[0].y;s.touch_x=170;s.touch_y=125;relay_controls_map(&u,&s,&c);assert(c.touches[0].x-start_x==120&&c.touches[0].y-start_y==79);
 int id=c.touches[0].id;s.touch_x=300;s.touch_y=230;relay_controls_map(&u,&s,&c);assert(c.touches[0].id==id&&c.touches[0].y==1079);
 touch(&u,&s,&c,10,100);assert(c.buttons&CHIAKI_CONTROLLER_BUTTON_TOUCHPAD);assert(c.touches[0].x==480);
 touch(&u,&s,&c,310,100);assert(c.touches[0].x==1440);
 touch(&u,&s,&c,280,50);assert(c.buttons&CHIAKI_CONTROLLER_BUTTON_TOUCHPAD);
 // Bottom-to-top swipes must not be captured by a click button.
 touch(&u,&s,&c,160,180);assert(c.touches[0].id>=0&&!(c.buttons&CHIAKI_CONTROLLER_BUTTON_TOUCHPAD));int initial_y=c.touches[0].y;s.touch_y=80;relay_controls_map(&u,&s,&c);assert(c.touches[0].y<initial_y);

 // Swipe contact survives the exact packet decoded on Android, then releases.
 touch(&u,&s,&c,160,120);uint8_t swipe[RELAY_CONTROL_BYTES];relay_control_encode(swipe,&c);assert(relay_control_decode(swipe,sizeof(swipe),&d));assert(d.touches[0].id==c.touches[0].id&&d.touches[0].y==c.touches[0].y);
 s.touch_down=false;relay_controls_map(&u,&s,&c);relay_control_encode(swipe,&c);assert(relay_control_decode(swipe,sizeof(swipe),&d)&&d.touches[0].id==-1);
 touch(&u,&s,&c,160,120);assert(c.touches[0].id!=d.touches[0].id);

 s.touch_down=false;s.held=RP_INPUT_SELECT;relay_controls_map(&u,&s,&c);assert(u.page==0);s.held=0;relay_controls_map(&u,&s,&c);
 touch(&u,&s,&c,260,15);assert(u.page==2);
 touch(&u,&s,&c,20,50);assert(c.buttons&CHIAKI_CONTROLLER_BUTTON_PS);
 touch(&u,&s,&c,140,50);assert(c.buttons&CHIAKI_CONTROLLER_BUTTON_SHARE);
 touch(&u,&s,&c,20,90);assert(u.motion);
 touch(&u,&s,&c,200,90);assert(u.yaw);
 touch(&u,&s,&c,20,130);assert(u.pitch);
 touch(&u,&s,&c,200,130);assert(u.roll);
 u.aim_gain_q=4;touch(&u,&s,&c,40,205);assert(u.gyro_aim);
 touch(&u,&s,&c,300,205);assert(u.aim_gain_q==5);touch(&u,&s,&c,160,205);assert(u.aim_gain_q==4);
 touch(&u,&s,&c,40,205);assert(!u.gyro_aim);
 touch(&u,&s,&c,20,170);assert(u.page==3);
 touch(&u,&s,&c,100,100);assert(u.page==0);
 // Consent is modal: SELECT cannot bypass it and game controls stay neutral.
 touch(&u,&s,&c,260,15);touch(&u,&s,&c,160,230);assert(u.page==4&&u.usage_selection==1);
 s.touch_down=false;s.held=RP_INPUT_SELECT|RP_INPUT_L;s.circle_x=156;relay_controls_map(&u,&s,&c);assert(u.page==4&&c.buttons==0&&c.l2_state==0&&c.left_x==0);
 s.held=RP_INPUT_DPAD_LEFT;relay_controls_map(&u,&s,&c);assert(u.usage_selection==0);
 s.held=RP_INPUT_A;relay_controls_map(&u,&s,&c);assert(u.usage_decision==1&&c.buttons==0);
 u.usage_decision=0;s.held=RP_INPUT_B;relay_controls_map(&u,&s,&c);assert(u.usage_decision==2);
 u.usage_decision=0;touch(&u,&s,&c,40,196);assert(u.usage_decision==1);
 u.usage_decision=0;touch(&u,&s,&c,200,196);assert(u.usage_decision==2);
 Motion3ds m;motion_3ds_reset(&m);for(unsigned i=0;i<30;i++)motion_3ds_update(&m,0,-512,0,20,0,0,10.f,10000+i*66667);motion_3ds_apply(&m,&c);assert(m.valid&&isfinite(c.orient_w));
 ChiakiControllerState inv=c;motion_3ds_configure(true,true,true);motion_3ds_apply(&m,&inv);assert(inv.gyro_x==-c.gyro_x&&inv.gyro_y==-c.gyro_y&&inv.gyro_z==-c.gyro_z);motion_3ds_configure(false,false,false);
 c.right_x=-12345;c.right_y=23456;c.buttons=CHIAKI_CONTROLLER_BUTTON_PS|CHIAKI_CONTROLLER_BUTTON_L3;c.touches[0].id=42;c.touches[0].x=1919;c.touches[0].y=1079;c.touch_id_next=43;
 uint8_t wire[RELAY_CONTROL_BYTES];relay_control_encode(wire,&c);assert(relay_control_decode(wire,sizeof(wire),&d));assert(d.buttons==c.buttons&&d.right_x==-12345&&d.right_y==23456&&d.touches[0].id==42&&d.touches[0].x==1919&&d.gyro_x==c.gyro_x&&d.orient_w==c.orient_w);
 assert(!relay_control_decode(wire,10,&d));wire[15]=2;assert(!relay_control_decode(wire,sizeof(wire),&d));wire[15]=1;control_float_write(wire+28,NAN);assert(!relay_control_decode(wire,sizeof(wire),&d));relay_control_encode(wire,&c);w16(wire+18,1920);assert(!relay_control_decode(wire,sizeof(wire),&d));
 chiaki_controller_state_set_idle(&c);relay_control_encode(wire,&c);assert(relay_control_decode(wire,sizeof(wire),&d)&&d.touches[0].id==-1&&d.buttons==0&&d.right_x==0);
}
''')
 includes=[root/'3ds',root/'common',app/'include',core/'lib/include',root/'android/app/src/main/cpp/generated']
 cmd=['cc','-O1','-g','-fsanitize=address,undefined']+['-I'+str(x) for x in includes]+[str(p/'check.c'),str(root/'3ds/controls.c'),str(app/'source/input_3ds.c'),str(root/'3ds/motion_3ds.c'),str(core/'lib/src/controller.c'),str(core/'lib/src/orientation.c'),'-lm','-o',str(p/'check')]
 subprocess.run(cmd,check=True);subprocess.run([p/'check'],check=True)
print('PASS buttons/sticks/touchpad/clicks/motion, original-3DS fallbacks and full-state encoding/validation')
