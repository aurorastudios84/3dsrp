#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
WIN_ROOT="$PWD"
export PATH="$WIN_ROOT/toolchain/root/usr/bin:$PATH"
export LD_LIBRARY_PATH="$WIN_ROOT/toolchain/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[ -f dist/3DSRP-Companion.exe ] || { echo 'Build the EXE first with ./build.sh' >&2; exit 1; }
cp README.txt dist/README.txt
python3 - <<'PY'
from pathlib import Path
w=Path.cwd()
r=w.parent.parent
sources=[w/'licenses/MinGW-w64.txt',w/'licenses/GCC-runtime.txt',r/'COPYING',r/'NOTICE.md',r/'relay/vendor/chiaki-upstream/COPYING',r/'relay/vendor/mbedtls/LICENSE',r/'relay/vendor/libjpeg-turbo-3.1.3/LICENSE.md',r/'source/chiaki-ng/third-party/jerasure/COPYING',r/'source/chiaki-ng/third-party/gf-complete/COPYING',r/'source/chiaki-ng/third-party/nanopb/LICENSE.txt',w/'vendor/ffmpeg-7.1.5/COPYING.LGPLv2.1',w/'vendor/ffmpeg-7.1.5/COPYING.LGPLv3',w/'vendor/opus-1.6.1/COPYING',r/'relay/android/app/src/main/assets/licenses/Jerasure-LICENSE',r/'relay/android/app/src/main/assets/licenses/gf-complete-LICENSE',r/'relay/android/app/src/main/assets/licenses/nanopb-LICENSE']
text=['3DSRP Companion for Windows\n\nIncludes Chiaki-ng, Mbed TLS, Jerasure, GF-Complete, nanopb, libjpeg-turbo, FFmpeg (H.264 decoder) and Opus. FFmpeg is built without GPL/nonfree options. No external codec installation is needed. Corresponding source and build scripts accompany this build.\n\nMinGW GCC runtime exception: https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html\n']
for p in sources:
 if p.is_file():text.append('\n'+'='*72+'\n'+str(p.relative_to(r))+'\n'+'='*72+'\n'+p.read_text(errors='replace'))
(w/'dist/THIRD-PARTY-LICENSES.txt').write_text('\n'.join(text))
PY
wixl -a x64 -o dist/3DSRP-Companion-42.0.3-x64.msi installer.wxs
python3 - <<'PY'
import hashlib,json,zipfile
from pathlib import Path
w=Path.cwd();d=w/'dist'
with zipfile.ZipFile(d/'3DSRP-Companion-42.0.3-x64-portable.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n in ['3DSRP-Companion.exe','README.txt','THIRD-PARTY-LICENSES.txt']:z.write(d/n,n)
files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(d.iterdir()) if p.suffix in ('.exe','.msi','.zip')}
(d/'SHA256SUMS.txt').write_text(''.join(f'{v}  {k}\n' for k,v in files.items()))
print(json.dumps({'artifacts':files,'signed':False,'runtime_tests':'Not run; user will test on Windows'},indent=2))
PY
