#!/usr/bin/env python3
"""Check Java validation and native mapping using the real upstream preset function."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];core=root/'vendor/chiaki-upstream';cpp=root/'android/app/src/main/cpp'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'ProfileCheck.java').write_text('''package com.aurora.relay;
public class ProfileCheck {public static void main(String[] args){
 if(StreamProfile.DEFAULT_HEIGHT!=360||StreamProfile.DEFAULT_BITRATE!=3000||StreamProfile.DEFAULT_FPS!=30)throw new AssertionError("Unexpected defaults");
 int[] h={360,540,720,1080},w={640,960,1280,1920};
 for(int i=0;i<h.length;i++)for(int b:new int[]{100,1000,50000})for(int fps:new int[]{30,60}){StreamProfile x=new StreamProfile(h[i],b,fps);if(x.width!=w[i]||x.fps!=fps||!x.json().equals("["+w[i]+","+h[i]+","+fps+","+b+"]")||!x.label().contains(fps+" fps"))throw new AssertionError();}
 if(new StreamProfile(360,3000).fps!=30)throw new AssertionError("Existing callers must default to 30fps");
 for(int fps:new int[]{-1,0,29,31,59,61,120}){try{new StreamProfile(360,3000,fps);throw new AssertionError();}catch(IllegalArgumentException expected){}}
 for(int b:new int[]{-1,0,99,50001,Integer.MAX_VALUE}){try{new StreamProfile(360,b);throw new AssertionError();}catch(IllegalArgumentException expected){}}
 try{new StreamProfile(480,1000);throw new AssertionError();}catch(IllegalArgumentException expected){}
 for(String s:new String[]{"","abc","1.5","999999999999"}){try{StreamProfile.parseBitrate(s);throw new AssertionError();}catch(IllegalArgumentException expected){}}
 if(StreamProfile.parseBitrate(" 2500 ")!=2500)throw new AssertionError();
}}''')
 subprocess.run(['javac','-d',tmp,root/'android/app/src/main/java/com/aurora/relay/StreamProfile.java',p/'ProfileCheck.java'],check=True);subprocess.run(['java','-cp',tmp,'com.aurora.relay.ProfileCheck'],check=True)
 (p/'native.c').write_text('''#include <assert.h>
#include "stream_profile.h"
int main(void){int h[]={360,540,720,1080},w[]={640,960,1280,1920},b[]={100,1000,50000};ChiakiConnectVideoProfile p;
 for(unsigned i=0;i<4;i++)for(unsigned j=0;j<3;j++)for(int fps=30;fps<=60;fps+=30){assert(relay_stream_profile(&p,h[i],b[j],fps));assert(p.width==w[i]&&p.height==h[i]&&p.max_fps==(unsigned)fps&&p.bitrate==b[j]&&p.codec==CHIAKI_CODEC_H264);}
 assert(!relay_stream_profile(&p,480,1000,60));assert(!relay_stream_profile(&p,360,99,60));assert(!relay_stream_profile(&p,360,50001,60));int invalid[]={-1,0,29,31,59,61,120};for(unsigned i=0;i<7;i++)assert(!relay_stream_profile(&p,360,3000,invalid[i]));}
''')
 subprocess.run(['cc','-D_GNU_SOURCE','-O1','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(cpp),'-I'+str(core/'lib/include'),'-I'+str(cpp/'generated'),'-I'+str(root.parent/'source/chiaki-ng/third-party/nanopb'),p/'native.c',core/'lib/src/session.c','-o',p/'native'],check=True)
 subprocess.run([p/'native'],check=True)
print('PASS all four resolutions and bitrate bounds map to 30/60fps H264 in Java and actual upstream native presets; malformed values rejected')
