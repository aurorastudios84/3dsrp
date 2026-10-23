#!/usr/bin/env python3
"""Fingerprint current application and core source, excluding builds and artifacts."""
import hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[2]
h=hashlib.sha256()
paths=[]
for folder in ('3ds/app/source','3ds/app/include','chiaki-ng/lib/src','chiaki-ng/lib/include'):
    paths.extend(p for p in (root/folder).rglob('*') if p.suffix in ('.h','.c') and p.name!='build_id.h')
paths.extend(root/p for p in ('3ds/app/Makefile','3ds/app/build.sh','chiaki-ng/cmake/3ds.cmake','chiaki-ng/lib/CMakeLists.txt'))
paths.extend(p for p in (root/'3ds/app/resources').rglob('*') if p.is_file())
paths.extend(root/p for p in ('3ds/build_usage_tls.sh','3ds/usage-tls/namespace.py'))
paths.extend(p for p in (root/'vendor/usage-mbedtls').rglob('*') if p.is_file() and
             (p.suffix in ('.h','.c') or p.name=='CMakeLists.txt'))
for p in sorted(paths):
    h.update(str(p.relative_to(root)).encode()+b'\0'+p.read_bytes())
print(h.hexdigest()[:24])
