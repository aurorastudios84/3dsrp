#!/usr/bin/env python3
"""Review diagnostics with a booted emulator and real HELLO packets; no console needed."""
from pathlib import Path
import os,json,re,socket,struct,subprocess as sp,threading,time,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
release=int(re.search(r'RELAY_RELEASE (\d+)',(ROOT/'common/wire.h').read_text())[1]);tag=0x52330000+release

def adb(*args):return sp.check_output(['adb',*args],timeout=90)
def nodes():
 adb('shell','uiautomator','dump','/sdcard/diagnostics-ui.xml')
 return list(ET.fromstring(adb('shell','cat','/sdcard/diagnostics-ui.xml')).iter('node'))
def find(text,direction='up'):
 for _ in range(16):
  items=nodes();match=next((n for n in items if n.get('text','')==text or text in n.get('text','')),None)
  if match is not None:return match,items
  width,height=map(int,re.findall(r'(\d+)x(\d+)',adb('shell','wm','size').decode())[-1]);x=str(width//2);low=str(height*8//10);high=str(height*3//10)
  adb('shell','input','swipe',x,low if direction=='up' else high,x,high if direction=='up' else low,'300')
 raise AssertionError('UI text not found: '+text)
def tap(n):
 x,y,xx,yy=map(int,re.findall(r'\d+',n.get('bounds')));adb('shell','input','tap',str((x+xx)//2),str((y+yy)//2))
def screenshot(name):
 (ROOT/f'output/diagnostics30-{name}.png').write_bytes(adb('exec-out','screencap','-p'))

if not os.environ.get('RELAY_SKIP_INSTALL'):
 adb('install','-r',str(ROOT/'android/app/build/outputs/apk/debug/app-debug.apk'))
adb('shell','pm','grant','com.aurora.relay','android.permission.POST_NOTIFICATIONS')
adb('shell','input','keyevent','KEYCODE_WAKEUP');adb('shell','wm','dismiss-keyguard')
adb('shell','am','force-stop','com.aurora.relay');adb('shell','am','start','-n','com.aurora.relay/.MainActivity')
time.sleep(2)
tap(find('Menu')[0]);tap(find('Diagnostics')[0]);find('Connect your 3DS to check versions');_,items=find('Apps & compatibility','down')
assert any(n.get('text')==f'Release {release}' for n in items)
assert any(n.get('text')=='Not connected' for n in items)
screenshot('idle')
tap(find('Test connection to 3DS')[0]);tap(find('Menu')[0]);tap(find('Diagnostics')[0])
try:adb('emu','redir','del','udp:29870')
except sp.CalledProcessError:pass
adb('emu','redir','add','udp:29870:29870')
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('0.0.0.0',0));s.settimeout(.3)
stop=threading.Event();errors=[];sent_tag=tag

def heartbeat():
 try:
  while not stop.is_set():
   p=struct.pack('>4sBBHIIHHIHHI',b'R3R1',3,0,32,0,sent_tag,0,1,0,0,0,12345)
   s.sendto(p,('127.0.0.1',29870))
   try:s.recv(1500)
   except socket.timeout:pass
   stop.wait(.25)
 except Exception as e:errors.append(str(e))
t=threading.Thread(target=heartbeat);t.start()
try:
 find('Versions match · ready');_,items=find('Apps & compatibility','down')
 assert sum(n.get('text')==f'Release {release}' for n in items)==2
 screenshot('matched')
 tap(find('Menu')[0]);tap(find('Live picture')[0]);_,items=find('3DS connected','down')
 assert all('(release ' not in n.get('text','') for n in items)
 screenshot('stream-status')
 sent_tag=0x52330017
 find('3DS version mismatch');tap(find('Menu')[0]);tap(find('Diagnostics')[0]);find('Version mismatch · update both apps')
 _,items=find('Apps & compatibility','down');assert any(n.get('text')=='Release 23' for n in items)
 screenshot('mismatch')
 sent_tag=tag;find('Versions match · ready');find('Health & processing');screenshot('health')
 assert not errors,errors
finally:
 stop.set();t.join();s.close();adb('shell','am','force-stop','com.aurora.relay')
(ROOT/'output/diagnostics30-ui-tests.json').write_text(json.dumps(dict(idle_versions=True,matched_versions=True,old_release_rejected=True,recovery=True,status_suffix_removed=True,physical_hardware_tested=False),indent=2)+'\n')
print('PASS diagnostics idle/matched/mismatched/recovered; old release rejected; stream status has no release suffix')
