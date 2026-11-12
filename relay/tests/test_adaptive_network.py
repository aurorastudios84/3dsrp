#!/usr/bin/env python3
"""Running Android AVC/Opus fixture: UDP feedback changes actual JPEG delivery."""
import json
import re
from pathlib import Path
import socket
import struct
import time

root=Path(__file__).resolve().parents[1]
TAG=int(re.search(r'RELAY_COMPAT_TAG (0x[0-9a-f]+)',(root/'common/wire.h').read_text())[1],16)
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));sock.settimeout(.1)
destination=('127.0.0.1',29870)
def packet(kind,session=0,payload=b'',stamp=12345):
    return struct.pack('>4sBBHIIHHIHHI',b'R3R1',kind,0,32,session,TAG if kind==3 else 0,0,1,len(payload),0,0,stamp)+payload
def feedback(session,t,phase):
    decode,pack,missing,age=(60000,5000,0,75000) if phase=='slow' else (10000,1000,0,20000)
    if phase=='pressure':missing=10
    body=struct.pack('>12I',1,1000,13,13*decode,13,13*pack,13,missing,13*age,13,age,60)
    return packet(13,session,body,1000+int(t*1000))

start=time.monotonic();last_hello=last_feedback=-10;session=0;frames={};diagnostics=[];audio=0;injected=False
while (elapsed:=time.monotonic()-start)<15:
    phase='slow' if elapsed<5 else 'fast' if elapsed<11 else 'pressure'
    if elapsed-last_hello>=.7:
        sock.sendto(packet(3),destination)
        if session:sock.sendto(packet(5,session),destination)
        last_hello=elapsed
    if session and elapsed-last_feedback>=1:
        sock.sendto(feedback(session,elapsed,phase),destination);last_feedback=elapsed
    if session and elapsed>2.5 and not injected:
        # Invalid generation and malformed payload must not poison the target.
        sock.sendto(feedback(session^1,elapsed,'pressure'),destination)
        sock.sendto(packet(13,session,b'bad',999999),destination);injected=True
    try:p,_=sock.recvfrom(1500)
    except socket.timeout:continue
    if len(p)<32:continue
    _,kind,_,_,token,seq,index,count,total,_,_,_=struct.unpack('>4sBBHIIHHIHHI',p[:32])
    if kind==3:session=token
    if kind==1:
        parts=frames.setdefault(seq,{'time':elapsed,'parts':set(),'count':count});parts['parts'].add(index)
    if kind==2:audio+=1
    if kind==8:diagnostics.append((elapsed,json.loads(p[32:])))
sock.sendto(packet(7,session),destination);sock.close()
assert session and audio>100 and diagnostics
targets={}
for phase,a,b in [('slow',3,5),('fast',9,11),('pressure',13,15)]:
    reports=[d for t,d in diagnostics if a<=t<b]
    assert reports,(phase,diagnostics)
    targets[phase]=reports[-1]['adaptive'][0]/100
    count=sum(a<=v['time']<b and len(v['parts'])==v['count'] for v in frames.values())
    targets[phase+'_delivered_fps']=count/(b-a)
assert 12<=targets['slow']<=14,targets
assert targets['fast']>targets['slow'],targets
assert targets['pressure']<targets['slow'],targets
assert abs(targets['slow_delivered_fps']-targets['slow'])<3,targets
assert targets['pressure_delivered_fps']<targets['slow_delivered_fps'],targets
last=diagnostics[-1][1]
assert len(last['phone_age_us'])==5 and 0<last['phone_age_us'][2]<2000000,last
assert last['adaptive'][4]<2500,last
if last['app_version']>=27:
    assert last['scale_calls']<=last['sent']+3,last
    assert last['scale_calls']<last['decoded']*.6,last
result={'targets_and_actual_delivery':targets,'audio_packets':audio,'last_diagnostics':last,'physical_3ds_tested':False}
(root/'output/adaptive-network-tests.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
