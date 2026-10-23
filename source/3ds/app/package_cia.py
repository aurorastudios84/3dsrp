#!/usr/bin/env python3
"""Package the built 3DSRP ELF as a region-free, New-3DS-only CIA.

Requires makerom 0.19.0, bannertool 1.2.3, Python Pillow and DejaVu fonts.
MAKEROM and BANNERTOOL may specify full executable paths.
"""
from pathlib import Path
import os
import subprocess
import wave
from PIL import Image, ImageDraw, ImageFont

APP = Path(__file__).resolve().parent
OUTPUT = APP / 'cia-build'
OUTPUT.mkdir(parents=True, exist_ok=True)
elf = APP / '3DSRP.elf'
if not elf.exists():
    raise SystemExit('Build the app first: bash 3ds/app/build.sh')

# Existing 3DSRP artwork and licensed DejaVu text; no downloaded media.
banner = Image.new('RGB', (256, 128), (9, 15, 32))
icon = Image.open(APP / 'resources/3dsrp-icon.png').convert('RGB')
banner.paste(icon.resize((88, 88), Image.Resampling.LANCZOS), (12, 20))
draw = ImageDraw.Draw(banner)
font_root = Path('/usr/share/fonts/truetype/dejavu')
draw.text((112, 35), '3DSRP', fill=(239, 248, 255),
          font=ImageFont.truetype(str(font_root / 'DejaVuSans-Bold.ttf'), 27))
draw.text((113, 76), 'Remote Play', fill=(122, 179, 255),
          font=ImageFont.truetype(str(font_root / 'DejaVuSans.ttf'), 16))
banner.save(OUTPUT / 'banner.png')
with wave.open(str(OUTPUT / 'silence.wav'), 'wb') as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(22050)
    wav.writeframes(bytes(11026))

bannertool = os.environ.get('BANNERTOOL', 'bannertool')
makerom = os.environ.get('MAKEROM', 'makerom')
def run(*args):
    subprocess.run([str(a) for a in args], check=True)
run(bannertool, 'makesmdh', '-s', '3DSRP', '-l',
    'PS4/PS5 Remote Play for New Nintendo 3DS', '-p', 'AuroraStudios84',
    '-i', APP / 'resources/3dsrp-icon.png', '-r', 'regionfree',
    '-f', 'visible,new3ds', '-o', OUTPUT / 'icon.icn')
run(bannertool, 'makebanner', '-i', OUTPUT / 'banner.png',
    '-a', OUTPUT / 'silence.wav', '-o', OUTPUT / 'banner.bnr')
run(makerom, '-f', 'cia', '-o', APP / '3DSRP.cia', '-target', 't',
    '-exefslogo', '-elf', elf, '-rsf', APP / 'cia.rsf', '-ver', '4',
    '-icon', OUTPUT / 'icon.icn', '-banner', OUTPUT / 'banner.bnr')
print(APP / '3DSRP.cia')
