#include "controls.h"
#include "wire.h"
#include "link_state.h"
#include <string.h>
enum { NONE,STICK,PAD,PAD_LEFT,PAD_RIGHT,PAN,TOGGLE,MINUS,PLUS,ADJUST,CENTRE,SURFACE0,SURFACE1,MENU,BACK,STATS,MOTION,YAW,PITCH,ROLL,L1,L3,R3,R1,OPTIONS,PS,CREATE,CLICK,R2,AIM,AIM_MINUS,AIM_PLUS,USAGE };
typedef struct {unsigned x,y,w,h,action;const char *label;} ControlRect;
/* These rectangles drive both rendering and touch-down capture. */
static const ControlRect header[]={{0,0,112,32,SURFACE0,"Right stick"},{112,0,112,32,SURFACE1,"Touchpad"},{224,0,96,32,MENU,"Menu"}};
static const ControlRect buttons[]={{0,32,48,40,L1,"L1"},{48,32,48,40,L3,"L3"},{96,32,48,40,R3,"R3"},{144,32,48,40,R1,"R1"},{192,32,64,40,OPTIONS,"Options"},{256,32,64,40,CLICK,"TP click"}};
static const ControlRect stick_buttons[]={{0,32,48,40,L1,"L1"},{48,32,48,40,L3,"L3"},{96,32,48,40,R3,"R3"},{144,32,48,40,R1,"R1"},{192,32,64,40,R2,"R2"},{256,32,64,40,OPTIONS,"Options"}};
static const ControlRect toolbar[]={{0,208,88,32,TOGGLE,"Magnify"},{88,208,32,32,MINUS,"-"},{120,208,56,32,NONE,""},{176,208,32,32,PLUS,"+"},{208,208,112,32,ADJUST,"Adjust"}};
static const ControlRect menu[]={{0,32,107,40,PS,"PS"},{107,32,106,40,CREATE,"Create"},{213,32,107,40,OPTIONS,"Options"},{0,72,160,40,MOTION,"Motion"},{160,72,160,40,YAW,"Invert yaw"},{0,112,160,40,PITCH,"Invert pitch"},{160,112,160,40,ROLL,"Invert roll"},{0,152,160,40,STATS,"Statistics"},{160,152,160,40,BACK,"Back to game"},{0,192,144,32,AIM,"Gyro aim OFF"},{144,192,40,32,AIM_MINUS,"-"},{184,192,96,32,NONE,"1.00x"},{280,192,40,32,AIM_PLUS,"+"},{0,224,320,16,USAGE,"Usage logging"}};
static const ControlRect centre={216,80,96,32,CENTRE,"Centre"};
static int clamp(int v,int low,int high){return v<low?low:v>high?high:v;}
static bool inside(const ControlRect *r,unsigned x,unsigned y){return x>=r->x&&x<r->x+r->w&&y>=r->y&&y<r->y+r->h;}
static unsigned hit(const ControlRect *r,unsigned n,unsigned x,unsigned y){for(unsigned i=0;i<n;i++)if(inside(&r[i],x,y))return r[i].action;return NONE;}
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static void page(RelayControls *u,unsigned p){u->page=p;if(p<2)u->last_surface=p;u->revision++;}
void relay_controls_map(RelayControls *u,const Input3dsSnapshot *snapshot,ChiakiControllerState *state){
 Input3dsSnapshot s=*snapshot;
 uint32_t pressed=s.held&~u->usage_keys;u->usage_keys=s.held;
 if(u->page==4){
  if(pressed&(RP_INPUT_DPAD_LEFT|RP_INPUT_DPAD_RIGHT)){u->usage_selection^=1;u->revision++;}
  if(pressed&RP_INPUT_A)u->usage_decision=u->usage_selection?2:1;
  if(pressed&RP_INPUT_B)u->usage_decision=2;
  if(s.touch_down&&!u->extra_down&&s.touch_y>=176&&s.touch_y<216){u->usage_selection=s.touch_x<160?0:1;u->usage_decision=u->usage_selection?2:1;u->revision++;}
  u->extra_down=s.touch_down;chiaki_controller_state_set_idle(state);return;
 }
 bool select=(s.held&RP_INPUT_SELECT)!=0;
 if(select&&!u->select_down){page(u,u->page<2?1-u->page:u->last_surface);u->suppress=true;u->gesture=NONE;}
 u->select_down=select;
 if(!s.touch_down){u->suppress=false;if(u->extra_down)u->revision++;u->extra_down=false;u->gesture=NONE;}
 bool down=s.touch_down&&!u->suppress;
 if(down&&!u->extra_down){
  unsigned x=s.touch_x,y=s.touch_y,a=NONE;
  if(u->page==3)a=BACK;
  else if(y<32)a=hit(header,COUNT(header),x,y);
  else if(u->page==2)a=hit(menu,COUNT(menu),x,y);
  else if(y<72)a=u->page==0?hit(stick_buttons,COUNT(stick_buttons),x,y):hit(buttons,COUNT(buttons),x,y);
  else if(y>=208)a=hit(toolbar,COUNT(toolbar),x,y);
  else if(u->adjust)a=inside(&centre,x,y)?CENTRE:PAN;
  else if(u->page==0)a=STICK;
  else a=x<32?PAD_LEFT:x>=288?PAD_RIGHT:PAD;
  u->gesture=a;u->origin_x=x;u->origin_y=y;u->revision++;
  switch(a){
   case SURFACE0:page(u,0);break;case SURFACE1:page(u,1);break;
   case USAGE:u->usage_return_page=2;u->usage_selection=u->usage_allowed?0:1;u->usage_failed=false;page(u,4);break;
   case MENU:page(u,2);break;case BACK:page(u,u->last_surface);break;case STATS:page(u,3);break;
   case TOGGLE:u->magnify=!u->magnify;if(!u->magnify)u->adjust=false;break;
   case MINUS:u->magnify=true;u->zoom_q=clamp((int)u->zoom_q-2,4,32);break;
   case PLUS:u->magnify=true;u->zoom_q=clamp((int)u->zoom_q+2,4,32);break;
   case ADJUST:u->magnify=true;u->adjust=!u->adjust;break;
   case CENTRE:u->crop_x=u->crop_y=32768;break;
   case AIM:u->gyro_aim=!u->gyro_aim;break;
   case AIM_MINUS:u->aim_gain_q=clamp((int)u->aim_gain_q-1,1,16);break;
   case AIM_PLUS:u->aim_gain_q=clamp((int)u->aim_gain_q+1,1,16);break;
   case MOTION:u->motion=!u->motion;break;case YAW:u->yaw=!u->yaw;break;case PITCH:u->pitch=!u->pitch;break;case ROLL:u->roll=!u->roll;break;
   case PAD:case PAD_LEFT:case PAD_RIGHT:u->touch.touch_id=u->touch.touch_id_next;u->touch.touch_id_next=(u->touch.touch_id_next+1)&127;break;
  }
 }
 s.touch_down=false;input_3ds_map(&s,&u->touch,state);
 if(down){
  switch(u->gesture){
   case L1:state->buttons|=CHIAKI_CONTROLLER_BUTTON_L1;break;case R1:state->buttons|=CHIAKI_CONTROLLER_BUTTON_R1;break;
   case L3:state->buttons|=CHIAKI_CONTROLLER_BUTTON_L3;break;case R3:state->buttons|=CHIAKI_CONTROLLER_BUTTON_R3;break;
   case OPTIONS:state->buttons|=CHIAKI_CONTROLLER_BUTTON_OPTIONS;break;case PS:state->buttons|=CHIAKI_CONTROLLER_BUTTON_PS;break;case CREATE:state->buttons|=CHIAKI_CONTROLLER_BUTTON_SHARE;break;case CLICK:state->buttons|=CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;break;
   case R2:state->r2_state=255; /* Hold fire while the same contact aims. */
    /* fall through */
   case STICK:{int x=clamp(((int)s.touch_x-u->origin_x)*32767/60,-32767,32767),y=clamp(((int)s.touch_y-u->origin_y)*32767/60,-32767,32767);state->right_x=x;state->right_y=y;break;}
   case PAN:u->crop_x=clamp((int)u->crop_x+((int)s.touch_x-u->origin_x)*256,0,65535);u->crop_y=clamp((int)u->crop_y+((int)s.touch_y-u->origin_y)*256,0,65535);u->origin_x=s.touch_x;u->origin_y=s.touch_y;break;
   case PAD:case PAD_LEFT:case PAD_RIGHT:
    state->touches[0].id=u->touch.touch_id;/* Twice the travel per stylus pixel, anchored at touch-down. */
    state->touches[0].x=clamp(u->origin_x*1919/319+((int)s.touch_x-u->origin_x)*3838/319,0,1919);
    state->touches[0].y=clamp((u->origin_y-72)*1079/135+((int)s.touch_y-u->origin_y)*2158/135,0,1079);
    if(u->gesture!=PAD){state->buttons|=CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;state->touches[0].x=u->gesture==PAD_LEFT?480:1440;state->touches[0].y=540;}break;
  }
 }
 state->touch_id_next=u->touch.touch_id_next;u->extra_down=down;
}
#ifdef __3DS__
#include <3ds.h>
#include <stdio.h>
#include "motion_3ds.h"
#include "touch_trail.h"
static TouchTrail swipe_trail;
static unsigned trail_x,trail_y;
static bool trail_contact;
uint64_t chiaki_time_now_monotonic_us(void){u64 t=svcGetSystemTick();return (t/SYSCLOCK_ARM11)*1000000+(t%SYSCLOCK_ARM11)*1000000/SYSCLOCK_ARM11;}
void relay_controls_sample(RelayControls *ui,unsigned held,bool connected,ChiakiControllerState *state){
 Input3dsSnapshot s={0};circlePosition circle={0},stick={0};touchPosition touch={0};
 s.held=((held&KEY_A)?RP_INPUT_A:0)|((held&KEY_B)?RP_INPUT_B:0)|((held&KEY_X)?RP_INPUT_X:0)|((held&KEY_Y)?RP_INPUT_Y:0)|((held&KEY_DUP)?RP_INPUT_DPAD_UP:0)|((held&KEY_DDOWN)?RP_INPUT_DPAD_DOWN:0)|((held&KEY_DLEFT)?RP_INPUT_DPAD_LEFT:0)|((held&KEY_DRIGHT)?RP_INPUT_DPAD_RIGHT:0)|((held&KEY_L)?RP_INPUT_L:0)|((held&KEY_R)?RP_INPUT_R:0)|((held&KEY_ZL)?RP_INPUT_ZL:0)|((held&KEY_ZR)?RP_INPUT_ZR:0)|((held&KEY_SELECT)?RP_INPUT_SELECT:0);
 hidCircleRead(&circle);hidCstickRead(&stick);s.circle_x=circle.dx;s.circle_y=circle.dy;s.cstick_x=stick.dx;s.cstick_y=stick.dy;s.touch_down=(held&KEY_TOUCH)!=0;if(s.touch_down){hidTouchRead(&touch);s.touch_x=touch.px;s.touch_y=touch.py;}
 relay_controls_map(ui,&s,state);trail_x=s.touch_x;trail_y=s.touch_y;trail_contact=s.touch_down&&(ui->gesture==PAD||ui->gesture==PAD_LEFT||ui->gesture==PAD_RIGHT);motion_3ds_configure(ui->yaw,ui->pitch,ui->roll);bool aiming=ui->gyro_aim&&(held&KEY_L);
 motion_3ds_poll(connected&&(ui->motion||ui->gyro_aim));
 if(ui->page!=4&&connected&&ui->motion&&!aiming)motion_3ds_apply_current(state);
 if(ui->page!=4&&connected&&aiming){ChiakiControllerState motion;chiaki_controller_state_set_idle(&motion);motion_3ds_apply_current(&motion);motion_3ds_aim(state,&motion,true,ui->aim_gain_q);}
}
void relay_controls_init(RelayControls *ui){memset(ui,0,sizeof(*ui));input_3ds_reset_capture(&ui->touch);ui->crop_x=ui->crop_y=32768;ui->zoom_q=8;ui->aim_gain_q=4;uint8_t b[7];FILE *f=fopen("sdmc:/3ds/3dsrp-relay/controls.bin","rb");if(f){size_t n=fread(b,1,7,f);if(n>=5&&!memcmp(b,"RC1",3)){ui->yaw=(b[3]&1)!=0;ui->pitch=(b[3]&2)!=0;ui->roll=(b[3]&4)!=0;if(n>=7){ui->gyro_aim=b[5]!=0;ui->aim_gain_q=clamp(b[6],1,16);}}fclose(f);}}
void relay_controls_save(const RelayControls *ui){uint8_t b[7]={'R','C','1',(ui->yaw?1:0)|(ui->pitch?2:0)|(ui->roll?4:0),0,ui->gyro_aim,ui->aim_gain_q};FILE *f=fopen("sdmc:/3ds/3dsrp-relay/controls.bin","wb");if(f){fwrite(b,1,7,f);fclose(f);}}
static PrintConsole *bottom_console;
static uint16_t composed[320*240],animated[320*240];
static const uint16_t *pending_panel;
void relay_controls_set_console(void *console){bottom_console=console;}
/* Presentation only: all hit rectangles and gesture handling above stay fixed. */
typedef struct { uint32_t offset; uint8_t width,height; int8_t x,y; uint8_t advance; } UiGlyph;
typedef struct { unsigned size; const unsigned char *pixels; const UiGlyph *glyphs; } UiFont;
#include "ui_fonts.h"
#define UI_RGB(r,g,b) ((uint16_t)(((r)>>3)<<11|((g)>>2)<<5|((b)>>3)))
#define UI_BACKGROUND UI_RGB(9,15,32)
#define UI_SURFACE UI_RGB(12,22,41)
#define UI_BUTTON UI_RGB(18,32,54)
#define UI_BORDER UI_RGB(49,68,94)
#define UI_ACTIVE UI_RGB(25,65,114)
#define UI_PRESSED UI_RGB(42,103,176)
#define UI_ACCENT UI_RGB(85,166,255)
#define UI_TEXT UI_RGB(239,245,255)
#define UI_MUTED UI_RGB(154,175,201)
static uint16_t blend_color(uint16_t bg,uint16_t fg,unsigned alpha){
 unsigned r=((bg>>11)*(255-alpha)+(fg>>11)*alpha+127)/255;
 unsigned g=(((bg>>5)&63)*(255-alpha)+((fg>>5)&63)*alpha+127)/255;
 unsigned b=((bg&31)*(255-alpha)+(fg&31)*alpha+127)/255;
 return (uint16_t)(r<<11|g<<5|b);
}
static void fill(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color){
 for(unsigned px=x;px<x+w&&px<320;px++)for(unsigned py=y;py<y+h&&py<240;py++)composed[px*240+239-py]=color;
}
static unsigned text_width(const char *text,const UiFont *f){
 unsigned width=0;for(const unsigned char *p=(const unsigned char *)text;*p;p++)width+=f->glyphs[*p>=32&&*p<127?*p-32:'?'-32].advance;return width;
}
static void caption(unsigned x,unsigned y,const char *text,const UiFont *f,uint16_t color,unsigned limit){
 int cursor=(int)x;
 for(const unsigned char *p=(const unsigned char *)text;*p;p++){
  const UiGlyph *g=&f->glyphs[*p>=32&&*p<127?*p-32:'?'-32];
  if(cursor+g->advance>(int)limit)break;
  for(unsigned gy=0;gy<g->height;gy++)for(unsigned gx=0;gx<g->width;gx++){
   int px=cursor+g->x+(int)gx,py=(int)y+g->y+(int)gy;
   if(px>=0&&px<(int)limit&&px<320&&py>=0&&py<240){unsigned i=(unsigned)px*240+239-(unsigned)py;composed[i]=blend_color(composed[i],color,f->pixels[g->offset+gy*g->width+gx]);}
  }
  cursor+=g->advance;
 }
}
/* The waiting screen uses the existing video texture and fonts: one image,
 * no extra GPU objects or full-rate overlays on the gameplay picture. */
static void status_box(uint16_t *pixels,unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color){
 for(unsigned py=y;py<y+h&&py<225;py++)for(unsigned px=x;px<x+w&&px<400;px++)pixels[py*400+px]=color;
}
static void status_caption(uint16_t *pixels,unsigned x,unsigned y,const char *text,const UiFont *font,uint16_t color){
 int cursor=(int)x;
 for(const unsigned char *p=(const unsigned char *)text;*p;p++){
  const UiGlyph *g=&font->glyphs[*p>=32&&*p<127?*p-32:'?'-32];if(cursor+g->advance>368)break;
  for(unsigned gy=0;gy<g->height;gy++)for(unsigned gx=0;gx<g->width;gx++){
   int px=cursor+g->x+(int)gx,py=(int)y+g->y+(int)gy;
   if(px>=0&&px<400&&py>=0&&py<225){unsigned i=(unsigned)py*400+(unsigned)px;pixels[i]=blend_color(pixels[i],color,font->pixels[g->offset+gy*g->width+gx]);}
  }
  cursor+=g->advance;
 }
}
void relay_connection_draw(uint16_t *pixels,unsigned view,unsigned source){
 bool test=source==RELAY_SOURCE_TEST;
 bool phone=view>=RELAY_VIEW_WAIT_PS5&&view<=RELAY_VIEW_SOURCE_FAILED;
 bool ps5=view==RELAY_VIEW_WAIT_VIDEO||view==RELAY_VIEW_VIDEO_STALLED||view==RELAY_VIEW_LIVE;
 uint16_t amber=UI_RGB(234,203,131),green=UI_RGB(131,224,173);
 status_box(pixels,0,0,400,225,UI_BACKGROUND);status_box(pixels,18,22,364,180,UI_SURFACE);status_box(pixels,18,22,3,180,amber);
 status_caption(pixels,32,35,"3DSRP  /  CONNECTION",&fonts[2],UI_MUTED);
 status_caption(pixels,32,63,test&&phone&&view==RELAY_VIEW_WAIT_VIDEO?"Waiting for test picture":relay_view_title(view),&fonts[5],UI_TEXT);
 const char *line1="Open the Android app and start the relay.",*line2="Check the phone address and Wi-Fi.";
 if(view==RELAY_VIEW_WAIT_PS5){line1="The phone relay is connected.";line2="Wake your console on the phone.";}
 else if(view==RELAY_VIEW_WAIT_VIDEO){line1="Waiting for the first picture.";line2="Video will appear automatically.";}
 else if(view==RELAY_VIEW_VIDEO_STALLED){line1="Relay connected. Video has stopped.";line2="Check the stream on your phone.";}
 else if(view==RELAY_VIEW_SOURCE_FAILED){line1="The phone relay is connected.";line2="Check the error in the Android app.";}
 else if(view==RELAY_VIEW_VERSION||view==RELAY_VIEW_FORMAT){line1="Update the Android and 3DS apps.";line2="Both apps must use the same release.";}
 status_caption(pixels,32,106,line1,&fonts[3],UI_MUTED);status_caption(pixels,32,129,line2,&fonts[3],UI_MUTED);
 status_caption(pixels,32,170,phone?"Relay: connected":"Relay: not connected",&fonts[2],phone?green:amber);
 status_caption(pixels,220,170,phone&&test?"Test mode":ps5?"Console: connected":"Console: waiting",&fonts[2],ps5?green:amber);
}
static void label(unsigned x,unsigned y,const char *text){caption(x,y,text,&fonts[1],UI_MUTED,312);}
/* Centre the visible glyph bounds, including the two-line block vertically
 * inside the unchanged control surface (x=0..319, y=72..207). */
typedef struct { int left,top,right,bottom; } TextBounds;
static TextBounds text_bounds(const char *text,const UiFont *f){
 TextBounds b={320,240,0,0};int cursor=0;
 for(const unsigned char *p=(const unsigned char *)text;*p;p++){
  const UiGlyph *g=&f->glyphs[*p>=32&&*p<127?*p-32:'?'-32];
  if(g->width&&g->height){
   int left=cursor+g->x,right=left+g->width,bottom=g->y+g->height;
   if(left<b.left)b.left=left;
   if(right>b.right)b.right=right;
   if(g->y<b.top)b.top=g->y;
   if(bottom>b.bottom)b.bottom=bottom;
  }
  cursor+=g->advance;
 }
 return b;
}
static void surface_instructions(const char *title,const char *hint){
 const UiFont *f=&fonts[1];TextBounds a=text_bounds(title,f),b=text_bounds(hint,f);
 int height=a.bottom-a.top+12+b.bottom-b.top;
 int top=72+(136-height)/2;
 caption((320-(a.right-a.left))/2-a.left,top-a.top,title,f,UI_MUTED,312);
 caption((320-(b.right-b.left))/2-b.left,top+(a.bottom-a.top)+12-b.top,hint,f,UI_MUTED,312);
}
static void line(unsigned x0,unsigned y0,unsigned x1,unsigned y1){fill(x0,y0,x1-x0+1,y1-y0+1,UI_BORDER);}
static void box(const RelayControls *u,ControlRect r,bool active){
 bool pressed=u->extra_down&&u->gesture==r.action&&r.action!=NONE;
 fill(r.x,r.y,r.w,r.h,pressed?UI_PRESSED:active?UI_ACTIVE:r.action==NONE?UI_SURFACE:UI_BUTTON);
 /* Each shared edge is drawn once; the outer edges remain inside the rectangle. */
 line(r.x,r.y,r.x+r.w-1,r.y);line(r.x,r.y,r.x,r.y+r.h-1);
 if(r.x+r.w==320||r.action==CENTRE)line(r.x+r.w-1,r.y,r.x+r.w-1,r.y+r.h-1);
 if((r.y+r.h==72&&u->page<2)||r.y+r.h==224||r.y+r.h==240||r.action==CENTRE)line(r.x,r.y+r.h-1,r.x+r.w-1,r.y+r.h-1);
 if(active)fill(r.x+4,r.y+r.h-4,r.w-8,2,UI_ACCENT);
 const UiFont *f=&fonts[2];unsigned n=text_width(r.label,f);
 caption(r.x+(r.w>n?(r.w-n)/2:0),r.y+(r.h-f->size)/2-1,r.label,f,UI_TEXT,r.x+r.w-3);
}
void relay_controls_draw(const RelayControls *u){
 if(!bottom_console)return;
 fill(0,0,320,240,UI_BACKGROUND);
 if(u->page==4){
  caption(16,14,"Usage statistics",&fonts[4],UI_TEXT,308);
  caption(16,48,"Allow AuroraStudios84 to collect the",&fonts[2],UI_TEXT,308);
  caption(16,65,"streaming duration from this homebrew?",&fonts[2],UI_TEXT,308);
  caption(16,92,"Only for usage statistics.",&fonts[2],UI_MUTED,308);
  caption(16,115,"Random IDs, dates and time played.",&fonts[2],UI_MUTED,308);
  caption(16,132,"Sent securely by your Android relay.",&fonts[2],UI_MUTED,308);
  caption(16,154,u->usage_failed?"Save failed. Off for this app launch.":"You can change this in Menu anytime.",&fonts[1],UI_MUTED,308);
  box(u,(ControlRect){8,176,148,40,NONE,"Allow"},u->usage_selection==0);
  box(u,(ControlRect){164,176,148,40,NONE,"No thanks"},u->usage_selection==1);
  label(16,224,"Choose with touch, or left/right and A");pending_panel=composed;return;
 }
 if(u->page<2)fill(0,72,320,136,UI_SURFACE);
 for(unsigned i=0;i<COUNT(header);i++)box(u,header[i],u->page==i);
 if(u->page==2){for(unsigned i=0;i<COUNT(menu);i++){
  ControlRect b=menu[i];char gain[24];
  if(b.action==USAGE)b.label=u->usage_allowed?"Usage logging: on":"Usage logging: off";
  if(b.action==AIM)b.label=u->gyro_aim?"Gyro aim ON":"Gyro aim OFF";
  if(i==11){snprintf(gain,sizeof(gain),"%u.%02ux",u->aim_gain_q/4,(u->aim_gain_q%4)*25);b.label=gain;}
  box(u,b,(b.action==MOTION&&u->motion)||(b.action==YAW&&u->yaw)||(b.action==PITCH&&u->pitch)||(b.action==ROLL&&u->roll)||(b.action==AIM&&u->gyro_aim));
 }}
 else if(u->page<2){
  for(unsigned i=0;i<COUNT(buttons);i++)box(u,u->page==0?stick_buttons[i]:buttons[i],false);
  if(u->adjust){surface_instructions("MOVE MAGNIFIED VIEW","Drag, then tap Done");box(u,centre,false);}
  else if(u->page==0){surface_instructions("RIGHT STICK","Touch and drag to look");}
  else {label(72,104,"DUALSENSE TOUCHPAD");label(40,128,"Swipe here; TP click = press");line(31,72,31,207);line(288,72,288,207);}
  for(unsigned i=0;i<COUNT(toolbar);i++){ControlRect b=toolbar[i];char z[32];if(i==0)b.label=u->magnify?"Mag ON":"Magnify";if(i==2){snprintf(z,sizeof(z),"%u.%ux",(u->magnify?u->zoom_q:4)/4,((u->magnify?u->zoom_q:4)%4)*25/10);b.label=z;}if(i==4)b.label=u->adjust?"Done":"Adjust";box(u,b,(i==0&&u->magnify)||(i==4&&u->adjust));}
 }
 pending_panel=composed;
}
/* Same blue, two-pixel radius and one-second fade as the New 3DS trail.
 * Clip to this panel's surface so strokes cannot cover navigation/buttons. */
static void trail_dot(int x,int y,unsigned alpha){
 uint16_t color=UI_RGB(69,145,255);
 for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++){
  int px=x+dx,py=y+dy;
  if(dx*dx+dy*dy<=4&&px>0&&px<319&&py>=72&&py<208){unsigned i=px*240+239-py;animated[i]=blend_color(composed[i],color,alpha);}
 }
}
void relay_controls_animate(const RelayControls *u,uint32_t now){
 if(u->page==3){touch_trail_update(&swipe_trail,false,false,0,0,now);pending_panel=NULL;return;}
 bool visible=u->page==1&&!u->adjust;
 if(!touch_trail_update(&swipe_trail,visible,visible&&trail_contact,trail_x,clamp((int)trail_y-72,0,135),now))return;
 memcpy(animated,composed,sizeof(animated));
 for(unsigned i=0;i<swipe_trail.count;i++){
  const TouchTrailPoint *p=&swipe_trail.points[(swipe_trail.first+i)%TOUCH_TRAIL_CAPACITY];
  unsigned age=now-p->time;if(age>=TOUCH_TRAIL_LIFETIME)continue;
  unsigned alpha=255u*(TOUCH_TRAIL_LIFETIME-age)/TOUCH_TRAIL_LIFETIME;
  if(i&&p->join){const TouchTrailPoint *q=&swipe_trail.points[(swipe_trail.first+i-1)%TOUCH_TRAIL_CAPACITY];
   int dx=(int)p->x-q->x,dy=(int)p->y-q->y,steps=abs(dx)>abs(dy)?abs(dx):abs(dy);
   for(int k=0;k<steps;k++)trail_dot(q->x+dx*k/steps,72+q->y+dy*k/steps,alpha);
  }
  trail_dot(p->x,72+p->y,alpha);
 }
 pending_panel=animated;
}
void relay_controls_present(void){
 if(!bottom_console||!pending_panel)return;
 memcpy(bottom_console->frameBuffer,pending_panel,sizeof(composed));
 GSPGPU_FlushDataCache(bottom_console->frameBuffer,sizeof(composed));pending_panel=NULL;
}
#endif
