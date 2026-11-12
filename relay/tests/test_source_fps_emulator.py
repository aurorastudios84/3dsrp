#!/usr/bin/env python3
"""Exercise both source cadences through Activity -> Service -> codec and saved UI state."""
from pathlib import Path
import json
import os
import re
import subprocess as s
import time
import xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[1]
def adb(*args):return s.check_output(['adb',*args],text=True)
def nodes():
 adb('shell','uiautomator','dump','/sdcard/source-fps-ui.xml')
 return list(ET.fromstring(adb('shell','cat','/sdcard/source-fps-ui.xml')).iter('node'))
def find(predicate):
 for _ in range(12):
  match=next((n for n in nodes() if predicate(n)),None)
  if match is not None:return match
  adb('shell','input','swipe','540','1900','540','850','250')
 raise AssertionError('UI control not found')
def tap(node):
 x,y,xx,yy=map(int,re.findall(r'\d+',node.get('bounds')))
 adb('shell','input','tap',str((x+xx)//2),str((y+yy)//2))

adb('install','-r',str(root/'android/app/build/outputs/apk/debug/app-debug.apk'))
adb('shell','pm','grant','com.aurora.relay','android.permission.POST_NOTIFICATIONS')
adb('emu','redir','del','udp:29870');adb('emu','redir','add','udp:29870:29870')
reports={}
for fps in [30,60]:
 adb('shell','am','force-stop','com.aurora.relay')
 adb('shell','input','keyevent','KEYCODE_WAKEUP');adb('shell','wm','dismiss-keyguard')
 adb('shell','am','start','-n','com.aurora.relay/.MainActivity','--ez','selftest','true','--ei','source_fps',str(fps),'--ei','image_size','2')
 time.sleep(2)
 s.run(['python3',str(root/'tests/emulator_transport.py')],check=True,stdout=s.DEVNULL,env={**os.environ,'RELAY_SOURCE_FPS':str(fps)})
 report=json.loads((root/'output/emulator-codec-tests.json').read_text())
 (root/f'output/source-fps-codec-{fps}.json').write_text(json.dumps(report,indent=2)+'\n')
 prefs=ET.fromstring(adb('shell','run-as','com.aurora.relay','cat','shared_prefs/MainActivity.xml'))
 assert next(n for n in prefs if n.get('name')=='source_fps').get('value')==str(fps)
 # Reloading the normal app must show the saved selection.
 adb('shell','am','force-stop','com.aurora.relay');adb('shell','am','start','-n','com.aurora.relay/.MainActivity')
 tap(find(lambda n:n.get('text','')=='Menu'))
 tap(find(lambda n:n.get('text','')=='Stream settings'))
 picker=find(lambda n:n.get('content-desc')=='Console source frame rate')
 assert picker.get('enabled')=='true'
 assert any(n.get('text')==f'{fps} fps' for n in picker.iter('node'))
 reports[fps]={'input_fps':report['phone_diagnostics']['input_fps'],'requested_profile':report['phone_diagnostics']['ps5_request'],'saved_and_reloaded':True}
adb('shell','am','force-stop','com.aurora.relay')
(root/'output/source-fps-emulator-tests.json').write_text(json.dumps(reports,indent=2)+'\n')
print(json.dumps(reports,indent=2))
