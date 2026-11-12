#!/usr/bin/env python3
"""Running emulator fixture integration: reject mismatched clients, accept matching, expire lost peers.
The emulator's UDP 29870 must be forwarded to host UDP 29870.
"""
import socket,struct,time,threading,subprocess,re,xml.etree.ElementTree as ET
from pathlib import Path
wire=(Path(__file__).resolve().parents[1]/'common/wire.h').read_text()
TAG=int(re.search(r'RELAY_COMPAT_TAG (0x[0-9a-f]+)',wire)[1],16)
RELEASE=int(re.search(r'RELAY_RELEASE (\d+)',wire)[1])
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('0.0.0.0',0));s.settimeout(.15)
def send(kind,session=0,tag=TAG,payload=b'',run=12345):
 p=struct.pack('>4sBBHIIHHIHHI',b'R3R1',kind,0,32,session,tag,0,1,len(payload),0,0,run)+payload;s.sendto(p,('127.0.0.1',29870))
def collect(seconds):
 end=time.monotonic()+seconds;packets=[]
 while time.monotonic()<end:
  try:packets.append(s.recv(1500))
  except socket.timeout:pass
 return packets
for tag in [0,TAG-1]:
 send(3,tag=tag);p=collect(.7);assert any(x[4]==11 and b'versions do not match' in x[32:] for x in p);assert not any(x[4] in [1,2] for x in p)
send(3);p=collect(1);hello=next(x for x in p if x[4]==3);session=struct.unpack_from('>I',hello,8)[0];assert struct.unpack_from('>I',hello,12)[0]==TAG and struct.unpack_from('>I',hello,28)[0]==RELEASE;assert any(x[4]==1 for x in p)
# Report advancing displayed frames and keep the link alive while reading UI.
stop=threading.Event()
def heartbeat():
 count=1
 while not stop.is_set():
  send(3);send(12,session,payload=struct.pack('>III',count,0,0));count+=1;stop.wait(.35)
t=threading.Thread(target=heartbeat);t.start()
try:
 time.sleep(1.1);subprocess.run(['adb','shell','uiautomator','dump','/sdcard/compat-check.xml'],stdout=subprocess.DEVNULL,check=True);xml=subprocess.check_output(['adb','shell','cat','/sdcard/compat-check.xml'],text=True);texts=[n.get('text','') for n in ET.fromstring(xml).iter('node')];assert any('3DS connected' in x for x in texts),texts;assert all('(release ' not in x for x in texts),texts
finally:stop.set();t.join()
# No heartbeat: media must stop once the three-second lease expires.
collect(3.5);assert not any(p[4] in [1,2] for p in collect(.6))
# Reconnect, then mismatch must revoke that same endpoint immediately.
send(3,run=54321);assert any(x[4]==1 for x in collect(.7));send(3,tag=0);p=collect(.3);assert any(x[4]==11 for x in p);assert not any(x[4] in [1,2] for x in collect(.5))
s.close();print('PASS emulator: mismatched clients blocked, compatible media + reported-display UI, timeout, reconnect and mismatch revocation')
