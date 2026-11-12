#!/usr/bin/env python3
from pathlib import Path
import tempfile,subprocess,re
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.c').write_text(r'''
#include "wire.h"
#include <assert.h>
int main(){assert(!relay_link_expired(1000,1001));assert(!relay_link_expired(5000,1000));assert(relay_link_expired(5001,1000));assert(!relay_link_expired(50,0xfffffffeu));assert(relay_link_expired(5000,0xfffffffeu));uint8_t p[RELAY_HEADER];relay_header(p,RELAY_HELLO,7,RELAY_COMPAT_TAG,0,1,0,240,135,23);assert(relay_hello_compatible(p,sizeof(p)));w32(p+12,0);assert(!relay_hello_compatible(p,sizeof(p)));w32(p+12,RELAY_COMPAT_TAG-1);assert(!relay_hello_compatible(p,sizeof(p)));w32(p+12,0x52330017);assert(!relay_hello_compatible(p,sizeof(p)));w32(p+12,RELAY_COMPAT_TAG);w16(p+24,272);w16(p+26,153);assert(!relay_hello_compatible(p,sizeof(p)));w16(p+24,320);w16(p+26,180);assert(relay_hello_compatible(p,sizeof(p)));for(unsigned n=0;n<RELAY_HEADER;n++)assert(!relay_hello_compatible(p,n));}
''');subprocess.run(['cc','-fsanitize=address,undefined','-I'+str(r/'common'),p/'check.c','-o',p/'check'],check=True);subprocess.run([p/'check'],check=True)
 (p/'CompatibilityTest.java').write_text('''package com.aurora.relay;
import java.nio.*;
public class CompatibilityTest {public static void main(String[] args){byte[] p=Protocol.packet(Protocol.HELLO,0,Protocol.COMPAT,0,1,0,0,0,123,null,0,0);if(!Protocol.compatibleHello(p,p.length))throw new AssertionError();ByteBuffer.wrap(p).putInt(12,0);if(Protocol.compatibleHello(p,p.length))throw new AssertionError();ByteBuffer.wrap(p).putInt(12,Protocol.COMPAT-1);if(Protocol.compatibleHello(p,p.length))throw new AssertionError();ByteBuffer.wrap(p).putInt(12,0x52330017);if(Protocol.compatibleHello(p,p.length))throw new AssertionError();for(int n=0;n<32;n++)if(Protocol.compatibleHello(p,n))throw new AssertionError();}}''')
 subprocess.run(['javac','-d',tmp,r/'android/app/src/main/java/com/aurora/relay/Protocol.java',p/'CompatibilityTest.java'],check=True);subprocess.run(['java','-cp',tmp,'com.aurora.relay.CompatibilityTest'],check=True)
c=(r/'common/wire.h').read_text();j=(r/'android/app/src/main/java/com/aurora/relay/Protocol.java').read_text();gradle=(r/'android/app/build.gradle').read_text();assert int(re.search(r'RELAY_RELEASE (\d+)',c)[1])==int(re.search(r'RELEASE=(\d+)',j)[1]);assert int(re.search(r'versionCode (\d+)',gradle)[1])==int(re.search(r'RELEASE=(\d+)',j)[1]);assert int(re.search(r'RELAY_COMPAT_TAG (0x[0-9a-f]+)',c)[1],16)==int(re.search(r'COMPAT=(0x[0-9a-f]+)',j)[1],16)

assert int(re.search(r'COMPAT=(0x[0-9a-f]+)',j)[1],16)==0x52330000+int(re.search(r'RELEASE=(\d+)',j)[1])
print('PASS C/Android handshake: matching releases, legacy/wrong release rejection, unsupported video-size rejection; build versions agree')
