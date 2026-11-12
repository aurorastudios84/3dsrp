#!/usr/bin/env python3
"""Emulator-only legacy migration regression; backs up and restores emulator app data."""
import subprocess as s, pathlib,time,json,xml.etree.ElementTree as E,shlex
A=['adb','-s','emulator-5554']
def adb(*a,**kw):return s.check_output(A+list(a),**kw)
def private(*a,input=None):return s.run(A+['shell','run-as com.aurora.relay '+shlex.join(a)],input=input,stdout=s.PIPE,stderr=s.PIPE,check=True).stdout
adb('shell','am','force-stop','com.aurora.relay')
backup=adb('exec-out','run-as','com.aurora.relay','tar','cf','-','files','shared_prefs');pathlib.Path('/tmp/relay-emulator-before-migration-test.tar').write_bytes(backup)
try:
 private('rm','-f','shared_prefs/consoles.xml')
 legacy=b'<map><string name="ip">192.168.1.99</string><string name="account">1</string><int name="source_bitrate" value="3000" /></map>'
 private('sh','-c','cat > shared_prefs/MainActivity.xml',input=legacy)
 pair=b'legacy-pair-preservation-fixture';private('sh','-c','cat > files/ps5-pair.bin',input=pair)
 adb('shell','am','start','-n','com.aurora.relay/.MainActivity');time.sleep(2)
 data=private('cat','shared_prefs/consoles.xml');items=json.loads(next(n.text for n in E.fromstring(data) if n.get('name')=='items'))
 assert len(items)==1 and items[0]['local']=='192.168.1.99' and items[0]['account']=='1'
 assert private('cat','files/console-'+items[0]['id']+'.bin')==pair
 assert private('cat','files/ps5-pair.bin')==pair
 adb('shell','am','force-stop','com.aurora.relay');adb('shell','am','start','-n','com.aurora.relay/.MainActivity');time.sleep(1)
 data=private('cat','shared_prefs/consoles.xml');assert len(json.loads(next(n.text for n in E.fromstring(data) if n.get('name')=='items')))==1
 print('PASS legacy migration copies pairing, preserves original, and does not duplicate entries on restart')
finally:
 adb('shell','am','force-stop','com.aurora.relay');private('rm','-rf','files','shared_prefs');private('tar','xf','-',input=backup)
