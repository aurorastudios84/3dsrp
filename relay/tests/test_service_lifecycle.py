#!/usr/bin/env python3
"""Android emulator regression: AVC/Opus relay survives background + screen-off, then releases locks.
Requires an already booted emulator; installs the built APK without clearing saved profiles.
"""
from pathlib import Path
import subprocess as s,time,json,os,xml.etree.ElementTree as ET,re
root=Path(__file__).resolve().parents[1]
def adb(*args):return s.check_output(['adb',*args],text=True)
adb('shell','am','force-stop','com.aurora.relay')
adb('install','-r',str(root/'android/app/build/outputs/apk/debug/app-debug.apk'))
adb('shell','pm','grant','com.aurora.relay','android.permission.POST_NOTIFICATIONS')
adb('shell','input','keyevent','KEYCODE_WAKEUP');adb('shell','wm','dismiss-keyguard')
adb('shell','am','start','-n','com.aurora.relay/.MainActivity','--ez','selftest','true','--ei','source_fps',os.environ.get('RELAY_SOURCE_FPS','30'),'--ei','image_size',os.environ.get('RELAY_IMAGE_SIZE','1'))
adb('emu','redir','del','udp:29870');adb('emu','redir','add','udp:29870:29870')
time.sleep(2)
reports={}
for mode in ['foreground','locked']:
 if mode=='locked':adb('shell','input','keyevent','KEYCODE_HOME');adb('shell','input','keyevent','KEYCODE_SLEEP')
 s.run(['python3',str(root/'tests/emulator_transport.py')],stdout=s.DEVNULL,check=True,env={**os.environ,'RELAY_KEEP_SESSION':'1'})
 reports[mode]=json.loads((root/'output/emulator-codec-tests.json').read_text())
 if mode=='locked':
  power=adb('shell','dumpsys','power');assert 'mWakefulness=Asleep' in power and "PARTIAL_WAKE_LOCK              '3ds-relay:stream'" in power
  service=adb('shell','dumpsys','activity','services','com.aurora.relay');assert 'isForeground=true' in service
adb('shell','input','keyevent','KEYCODE_WAKEUP');adb('shell','wm','dismiss-keyguard')
adb('shell','am','start','-n','com.aurora.relay/.MainActivity')
for _ in range(8):
 adb('shell','uiautomator','dump','/sdcard/relay-lifecycle.xml')
 nodes=ET.fromstring(adb('shell','cat','/sdcard/relay-lifecycle.xml')).iter('node')
 stop=next((n for n in nodes if n.get('text','').lower() in ('disconnect','stop relay')),None)
 if stop is not None:break
 # The console/address panels can put Stop below the initial viewport.
 adb('shell','input','swipe','540','1900','540','900','300')
else:raise AssertionError('Stop relay button was not found after scrolling')
x,y,xx,yy=map(int,re.findall(r'\d+',stop.get('bounds')))
adb('shell','input','tap',str((x+xx)//2),str((y+yy)//2))
for _ in range(30):
 if 'isForeground=true' not in adb('shell','dumpsys','activity','services','com.aurora.relay'):break
 time.sleep(.2)
else:raise AssertionError('Service did not stop')
held=adb('shell','dumpsys','power').split('Wake Locks:')[-1].split('Suspend Blockers:')[0]
assert "PARTIAL_WAKE_LOCK              '3ds-relay:stream'" not in held
reports['stop_releases_service_and_wake_lock']=True;reports['physical_phone_or_console_tested']=False
(root/'output/service-lifecycle-tests.json').write_text(json.dumps(reports,indent=2)+'\n')
print('PASS foreground + screen-off AVC/Opus streaming, one service, Stop releases service and CPU wake lock')
