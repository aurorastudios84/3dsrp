#!/usr/bin/env python3
"""Render the 30-second Reddit promo. Requires Pillow, ffmpeg and DejaVu Sans.
Usage: python3 promo/render.py /path/to/assets /path/to/output
Assets: footage/{gameplay,hero,touchpad}.mp4 and footage-soundtrack.wav.
Original recordings are shown at normal speed; all gameplay is New 3DS footage.
"""
from pathlib import Path
import sys,subprocess
from PIL import Image,ImageDraw,ImageFont
assets=Path(sys.argv[1]); out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
W,H=1920,1080; bg='#101b22'; white='#f7f5e9'; lime='#d7fa88'; muted='#acb9bc'
fontroot=Path('/usr/share/fonts/truetype/dejavu')
def font(n,bold=False):return ImageFont.truetype(str(fontroot/('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf')),n)
def canvas():
 im=Image.new('RGB',(W,H),bg);d=ImageDraw.Draw(im);d.rounded_rectangle((96,65,252,111),radius=23,fill=lime);d.text((116,71),'3DSRP',font=font(25,True),fill=bg);d.text((1425,75),'REMOTE PLAY / HOMEBREW',font=font(20),fill=muted);return im,d
def save(im,name):p=out/(name+'.png');im.save(p);return p
im,d=canvas();d.text((96,245),'Your PlayStation.',font=font(108,True),fill=white);d.text((96,390),'On your 3DS.',font=font(108,True),fill=lime);d.text((102,615),'PS4 + PS5 Remote Play',font=font(42),fill=white);d.line((96,840,1824,840),fill='#3c4c50',width=2);d.text((96,885),'ONE PROJECT. MORE WAYS TO PLAY.',font=font(28),fill=muted);intro=save(im,'intro')
im,d=canvas();d.text((96,195),'Pick your handheld.',font=font(82,True),fill=white)
for x in [96,984]:d.rounded_rectangle((x,365,x+840,850),radius=26,fill='#1d3036')
d.text((142,415),'NEW 3DS / NEW 2DS XL',font=font(31,True),fill=lime);d.text((142,505),'Play directly.',font=font(52,True),fill=white);d.text((142,605),'PlayStation  →  New 3DS',font=font(32),fill=white);d.text((142,710),'No companion needed',font=font(28),fill=muted)
d.text((1030,415),'ORIGINAL 3DS / 2DS',font=font(31,True),fill=lime);d.text((1030,505),'Add a companion.',font=font(48,True),fill=white);d.text((1030,605),'Android or Windows',font=font(36),fill=white);d.text((1030,710),'PlayStation  →  Companion  →  3DS',font=font(26),fill=muted)
d.text((96,926),'Same local network · Homebrew setup required',font=font(28),fill=muted);paths=save(im,'paths')
im,d=canvas();d.text((96,235),'Keep playing.',font=font(126,True),fill=lime);d.text((102,455),'3DSRP',font=font(72,True),fill=white);d.text((102,570),'Source + downloads',font=font(35),fill=muted);d.text((102,645),'github.com/aurorastudios84/3dsrp',font=font(51,True),fill=white);d.line((96,850,1824,850),fill='#3c4c50',width=2);d.text((96,898),'Community homebrew · Not affiliated with Sony or Nintendo',font=font(26),fill=muted);end=save(im,'end')
# Transparent overlays keep the original footage visible without suggesting it
# demonstrates the Original 3DS relay or a companion app.
def overlay(title,name):
 im=Image.new('RGBA',(W,H),(0,0,0,0));d=ImageDraw.Draw(im)
 d.text((96,32),title,font=font(46,True),fill=white);d.text((96,1014),'3DSRP  /  REAL NEW 3DS FOOTAGE · NORMAL SPEED',font=font(23),fill=lime)
 return save(im,name)
sequence=[(intro,3,None),(assets/'footage/gameplay.mp4',6,overlay('Your games. Your handheld.','gameplay-label')),(assets/'footage/hero.mp4',5,overlay('PlayStation Remote Play, on New 3DS.','hero-label')),(paths,7,None),(assets/'footage/touchpad.mp4',3,overlay('Touch controls on the second screen.','touch-label')),(end,6,None)]
segments=[]
for i,(src,duration,label) in enumerate(sequence):
 target=out/f'part-{i}.mp4';cmd=['ffmpeg','-y','-loglevel','error']
 if label is None:cmd+=['-loop','1','-i',str(src),'-vf','fps=30,format=yuv420p']
 else:
  cmd+=['-i',str(src),'-i',str(label),'-filter_complex',"[0:v]fps=30,scale=1728:900:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=0x101b22,setsar=1[v];[v][1:v]overlay=0:0,format=yuv420p"]
 cmd+=['-t',str(duration),'-an','-c:v','libx264','-preset','fast','-crf','19','-threads','4','-map_metadata','-1',str(target)]
 subprocess.run(cmd,check=True);segments.append(target)
manifest=out/'concat.txt';manifest.write_text(''.join("file '"+p.name+"'\n" for p in segments))
subprocess.run(['ffmpeg','-y','-loglevel','error','-f','concat','-safe','0','-i',str(manifest),'-i',str(assets/'footage-soundtrack.wav'),'-map','0:v','-map','1:a','-c:v','copy','-af','afade=t=in:d=0.5,afade=t=out:st=28:d=2,volume=0.6','-c:a','aac','-b:a','192k','-t','30','-movflags','+faststart','-map_metadata','-1',str(out/'3DSRP-Reddit.mp4')],check=True)
print(out/'3DSRP-Reddit.mp4')
