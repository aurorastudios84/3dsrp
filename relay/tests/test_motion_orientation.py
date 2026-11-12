#!/usr/bin/env python3
"""Real motion pipeline: tilted startup, large-angle sweeps, pauses and invalid sensors."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[1];core=r/'vendor/chiaki-upstream'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.c').write_text(r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "motion_3ds.h"
static double maximum_error;
static void gravity(const ChiakiControllerState *c,double *a){double x=c->orient_x,y=c->orient_y,z=c->orient_z,w=c->orient_w;a[0]=2*(x*y+w*z);a[1]=1-2*(x*x+z*z);a[2]=2*(y*z-w*x);}
static double dot(const ChiakiControllerState *a,const ChiakiControllerState *b){return a->orient_x*b->orient_x+a->orient_y*b->orient_y+a->orient_z*b->orient_z+a->orient_w*b->orient_w;}
static void sample(Motion3ds *m,ChiakiControllerState *c,const double *a,const double *g,uint32_t now){
 motion_3ds_update(m,lround(a[0]*512),lround(-a[1]*512),lround(a[2]*512),lround(-g[0]*180/M_PI*10),lround(g[1]*180/M_PI*10),lround(-g[2]*180/M_PI*10),10,now);
 chiaki_controller_state_set_idle(c);motion_3ds_apply(m,c);
 double n=dot(c,c);assert(isfinite(n)&&fabs(n-1)<.00001);
}
int main(){
 // Every startup sample must agree with gravity immediately, with no warmup oscillation.
 for(int axis=0;axis<3;axis++)for(int degrees=-170;degrees<=170;degrees+=10){
  Motion3ds m;motion_3ds_reset(&m);ChiakiControllerState want,c,previous;chiaki_controller_state_set_idle(&want);
  double theta=degrees*M_PI/180;want.orient_w=cos(theta/2);if(axis==0)want.orient_x=sin(theta/2);if(axis==1)want.orient_y=sin(theta/2);if(axis==2)want.orient_z=sin(theta/2);
  double a[3],g[3]={0};gravity(&want,a);
  for(int i=0;i<45;i++){sample(&m,&c,a,g,100000+i*66667);double predicted[3];gravity(&c,predicted);for(int k=0;k<3;k++)assert(fabs(predicted[k]-a[k])<.004);if(i)assert(fabs(dot(&c,&previous))>.9999);previous=c;}
 }
 // Cross +/-90 and +/-170 degrees in both directions on all three axes.
 for(int axis=0;axis<3;axis++)for(int hz=15;hz<=30;hz+=15){
  Motion3ds m;motion_3ds_reset(&m);ChiakiControllerState c,previous,want;double last=0;uint32_t now=100000;double base[3]={0,1,0},zero[3]={0};sample(&m,&c,base,zero,now);previous=c;
  for(int i=1;i<=720;i++){
   double theta=170*M_PI/180*sin(2*M_PI*i/720);chiaki_controller_state_set_idle(&want);want.orient_w=cos(theta/2);if(axis==0)want.orient_x=sin(theta/2);if(axis==1)want.orient_y=sin(theta/2);if(axis==2)want.orient_z=sin(theta/2);
   double a[3],g[3]={0};gravity(&want,a);g[axis]=(theta-last)*hz;now+=1000000/hz;sample(&m,&c,a,g,now);
   double d=fmin(1,fabs(dot(&c,&want))),error=2*acos(d);if(error>maximum_error)maximum_error=error;
   if(error>.12){fprintf(stderr,"axis %d hz %d theta %.3f error %.3f\n",axis,hz,theta,error);return 1;}
   assert(fabs(dot(&c,&previous))>.99);previous=c;last=theta;
  }
 }
 // A pause re-seeds from gravity without reintroducing aggressive warmup.
 Motion3ds m;motion_3ds_reset(&m);ChiakiControllerState c;double a[3]={0,cos(.7),-sin(.7)},g[3]={0};sample(&m,&c,a,g,100000);sample(&m,&c,a,g,1000000);double out[3];gravity(&c,out);assert(fabs(out[2]-a[2])<.004);
 motion_3ds_update(&m,0,0,0,0,0,0,10,2000000);assert(!m.valid);
 motion_3ds_update(&m,0,-512,0,0,0,0,0,2000010);assert(!m.valid);
 ChiakiControllerState aim,motion;chiaki_controller_state_set_idle(&aim);chiaki_controller_state_set_idle(&motion);
 aim.l2_state=255;motion.gyro_y=-.5f;motion.gyro_x=.25f;
 motion_3ds_aim(&aim,&motion,false,4);assert(aim.right_x==0&&aim.right_y==0);
 motion_3ds_aim(&aim,&motion,true,4);assert(aim.right_x>0&&aim.right_y<0&&aim.l2_state==255);int normal=aim.right_x;
 chiaki_controller_state_set_idle(&aim);motion_3ds_aim(&aim,&motion,true,8);assert(aim.right_x>normal);
 chiaki_controller_state_set_idle(&aim);motion.gyro_x=.01f;motion.gyro_y=-.01f;motion_3ds_aim(&aim,&motion,true,4);assert(!aim.right_x&&!aim.right_y);
 aim.right_x=32000;motion.gyro_y=-30;motion_3ds_aim(&aim,&motion,true,16);assert(aim.right_x==32767);
 motion.gyro_y=NAN;motion.gyro_x=NAN;aim.right_x=123;aim.right_y=-456;motion_3ds_aim(&aim,&motion,true,4);assert(aim.right_x==123&&aim.right_y==-456);
 printf("PASS tilted startup and +/-170-degree motion sweeps at 15/30 Hz; max angular error %.2f degrees; pause/invalid sensor handling\n",maximum_error*180/M_PI);
}
''')
 includes=[r/'3ds',core/'lib/include',r/'android/app/src/main/cpp/generated']
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined']+['-I'+str(x) for x in includes]+[str(p/'check.c'),str(r/'3ds/motion_3ds.c'),str(core/'lib/src/orientation.c'),str(core/'lib/src/controller.c'),'-lm','-o',str(p/'check')],check=True)
 subprocess.run([p/'check'],check=True)
