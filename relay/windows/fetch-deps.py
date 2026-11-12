#!/usr/bin/env python3
"""Fetch exact public codec source archives; verify bytes before extracting."""
import hashlib
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parent
DEPENDENCIES = {
    'ffmpeg-7.1.5.tar.xz': (
        'https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz',
        'de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f'),
    'opus-1.6.1.tar.gz': (
        'https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz',
        '6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1'),
}

def main():
    vendor = ROOT / 'vendor'
    vendor.mkdir(exist_ok=True)
    for name, (url, expected) in DEPENDENCIES.items():
        path = vendor / name
        if not path.exists():
            with urllib.request.urlopen(url, timeout=90) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError(f'Checksum mismatch: {name}')
            path.write_bytes(data)
        elif hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Checksum mismatch: {path}')
        print(f'Verified source archive: {name}')

if __name__ == '__main__':
    main()
