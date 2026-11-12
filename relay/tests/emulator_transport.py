#!/usr/bin/env python3
"""Validate the running Android fixture mode through emulator UDP forwarding.

Start the app with --ez selftest true and forward host UDP29870 to guest29870.
This checks the Android code; it does not benchmark the 3DS or authenticate PS5.
"""
import socket,struct,time,json,os,re
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
TAG=int(re.search(r'RELAY_COMPAT_TAG (0x[0-9a-f]+)',(ROOT/'common/wire.h').read_text())[1],16)
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('0.0.0.0',0));s.settimeout(.2)
def packet(kind,session=0):return struct.pack('>4sBBHIIHHIHHI',b'R3R1',kind,0,32,session,TAG if kind==3 else 1,0,1,0,0,0,12345)
start=time.monotonic();hello=0;counts={};frames={};session=0;image=None;pcm=[];diagnostics=None;arrivals={}
while time.monotonic()-start<6:
 now=time.monotonic()
 if now-hello>.7:s.sendto(packet(3),('127.0.0.1',29870));hello=now
 try:p,addr=s.recvfrom(1500)
 except socket.timeout:continue
 if len(p)<32:continue
 magic,kind,flags,header,token,seq,index,count,total,w,h,stamp=struct.unpack('>4sBBHIIHHIHHI',p[:32]);assert magic==b'R3R1' and header==32
 counts[kind]=counts.get(kind,0)+1
 if kind==3:
  session=token;s.sendto(packet(5,session),('127.0.0.1',29870))
  # Exercise the new JNI packet route; fixtures deliberately have no PS5 session.
  controls=bytearray(68);controls[15]=1;controls[16]=controls[22]=255
  struct.pack_into('>f',controls,44,1.0);struct.pack_into('>f',controls,64,1.0)
  head=struct.pack('>4sBBHIIHHIHHI',b'R3R1',9,0,32,session,0,0,1,68,0,0,1)
  s.sendto(head+controls,('127.0.0.1',29870))
  controls[15]=255;s.sendto(head+controls,('127.0.0.1',29870))
 if kind==1:
  assert (w,h) in [(240,135),(320,180),(400,225)] and 0<total<=98304
  arrivals.setdefault(seq,[]).append(time.monotonic())
  parts=frames.setdefault(seq,{});parts[index]=p[32:]
  if len(parts)==count:image_size=(w,h);image=b''.join(parts[i] for i in range(count));assert len(image)==total
 if kind==8:diagnostics=json.loads(p[32:].decode())
 if kind==2:assert total==480;assert w==12000 and h==1 and total==len(p)-32;pcm+=list(struct.unpack('<'+'h'*((len(p)-32)//2),p[32:]))
if not os.environ.get('RELAY_KEEP_SESSION'):s.sendto(packet(7,session),('127.0.0.1',29870))
s.close()
assert session and image and counts.get(2,0)>10
(ROOT/'output/decoded-fixture.jpg').write_bytes(image);assert Image.open(ROOT/'output/decoded-fixture.jpg').size==image_size
assert diagnostics and len(diagnostics['bridge'])==4 and diagnostics['app_version']==int(re.search(r'versionCode (\d+)',(ROOT/'android/app/build.gradle').read_text())[1]) and diagnostics['core_backend']=='chiaki-ng-v1.10.0' and diagnostics['scale_native'] and diagnostics['native_check_us']>0
expected_fps=int(os.environ.get('RELAY_SOURCE_FPS','30'))
expected_height=int(os.environ.get('RELAY_SOURCE_HEIGHT','360'))
assert diagnostics['ps5_request']==[{360:640,540:960,720:1280,1080:1920}[expected_height],expected_height,expected_fps,3000]
assert diagnostics['input_fps']>expected_fps*.75 and diagnostics['sent_fps']<=31
assert len(frames)<=186, 'JPEG output must remain capped near 30fps over six seconds'
assert len(diagnostics['jpeg_delivery'])==5 and diagnostics['jpeg_delivery'][1]>0 and diagnostics['jpeg_delivery'][3]>0
if diagnostics['app_version']>=27:
 assert diagnostics['scale_calls']<=diagnostics['sent']+3,diagnostics
 assert diagnostics['scale_calls']<diagnostics['decoded']*(.6 if expected_fps==60 else .9),diagnostics
spans=[(a[-1]-a[0])*1000 for a in arrivals.values() if len(a)>=2]
if spans:assert sorted(spans)[len(spans)//2]>.5, 'Multi-fragment JPEGs must be paced'
else:assert all(len(parts)==1 for parts in frames.values()), 'Only single-fragment pictures may lack pacing spans'
assert diagnostics['full_controls']==0 # no fabricated controller success without a PS5 session
assert diagnostics['connection']==[0]*3 and diagnostics['source_audio']==[48000,2,480]
assert len(diagnostics['timing_us'])==4 and all(len(x)==7 and x[0]>0 for x in diagnostics['timing_us'])
assert min(pcm)<-100 and max(pcm)>100
result={'emulator_avc_opus_to_jpeg_pcm':True,'duration_seconds':6,'video_sequences':len(frames),'packets_by_type':counts,'pcm_min':min(pcm),'pcm_max':max(pcm),'physical_3ds_performance_tested':False,'phone_diagnostics':diagnostics}
(ROOT/'output/emulator-codec-tests.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
