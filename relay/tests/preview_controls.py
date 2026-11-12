#!/usr/bin/env python3
"""Render the actual bottom-screen compositor on the host, without a 3DS SDK.

Only platform sampling/presentation are omitted. No duplicate drawing code.
Writes native 320x240 PNGs and a 2x contact sheet to output/controls-preview.
"""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
app = root / '3ds/shared'
source = (root / '3ds/controls.c').read_text()
prefix = source[:source.index('#ifdef __3DS__')]
renderer = source[source.index('static PrintConsole *bottom_console;'):source.index('void relay_controls_present(void)')]
shim = '''
#include <stdio.h>
#include "touch_trail.h"
typedef struct { uint16_t *frameBuffer; } PrintConsole;
static TouchTrail swipe_trail;
static unsigned trail_x,trail_y;
static bool trail_contact;
'''
harness = r'''
static void save(const char *name,const RelayControls *u){
 relay_controls_draw(u);
 FILE *f=fopen(name,"wb");if(!f)exit(2);fprintf(f,"P6\n320 240\n255\n");
 for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++){
  unsigned p=pending_panel[x*240+239-y];
  unsigned char rgb[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);
 }fclose(f);
}
int main(void){
 PrintConsole console={0};relay_controls_set_console(&console);
 uint16_t screen[400*225];
 for(unsigned view=0;view<=RELAY_VIEW_FORMAT;view++){
  if(view==RELAY_VIEW_LIVE)continue;relay_connection_draw(screen,view,RELAY_SOURCE_ACTIVE);
  char name[64];snprintf(name,sizeof(name),"connection-%u.ppm",view);FILE *f=fopen(name,"wb");if(!f)return 4;
  fprintf(f,"P6\n400 225\n255\n");
  for(unsigned i=0;i<400*225;i++){unsigned p=screen[i];unsigned char c[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(c,1,3,f);}fclose(f);
 }
 RelayControls u={0};u.zoom_q=8;u.aim_gain_q=4;
 save("stick.ppm",&u);
 u.page=1;save("touchpad.ppm",&u);
 u.page=0;u.magnify=true;save("magnify.ppm",&u);
 u.adjust=true;save("adjust.ppm",&u);
 u.adjust=false;u.extra_down=true;u.gesture=R2;save("pressed.ppm",&u);
 u.extra_down=false;u.page=2;save("menu.ppm",&u);
 u.motion=u.yaw=u.pitch=u.roll=u.gyro_aim=true;u.aim_gain_q=16;save("menu-active.ppm",&u);
 u.page=4;u.usage_selection=1;save("usage-consent.ppm",&u);
 u.page=1;u.magnify=false;relay_controls_draw(&u);trail_x=160;trail_y=120;trail_contact=true;
 relay_controls_animate(&u,100);relay_controls_animate(&u,600);
 /* Faded strokes must recover the new surface, including underlying labels. */
 trail_contact=false;relay_controls_animate(&u,1600);
 if(memcmp(pending_panel,composed,sizeof(composed)))return 3;
 return 0;
}
'''
out = root / 'output/controls-preview'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'preview.c').write_text(prefix + shim + renderer + harness)
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-I'+str(root/'3ds'), '-I'+str(root/'common'),
                    '-I'+str(app/'include'), '-I'+str(root/'vendor/chiaki-upstream/lib/include'),
                    str(tmp/'preview.c'), '-o', str(tmp/'preview')], check=True)
    subprocess.run([tmp/'preview'], cwd=out, check=True)
    names = ['stick', 'touchpad', 'magnify', 'adjust', 'pressed', 'menu', 'menu-active', 'usage-consent']
    sheet = Image.new('RGB', (1280, 4*508), '#090f20')
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(names):
        ppm = out / (name+'.ppm')
        im = Image.open(ppm)
        im.save(out / (name+'.png'))
        x, y = (i % 2)*640, (i // 2)*508
        draw.text((x+8,y+6), name, fill='#eff5ff')
        sheet.paste(im.resize((640,480), Image.Resampling.NEAREST), (x,y+24))
        ppm.unlink()
    sheet.save(out / 'contact-sheet.png')
    for ppm in out.glob('connection-*.ppm'):
        Image.open(ppm).save(ppm.with_suffix('.png'));ppm.unlink()
print('PASS actual compositor: seven native-size previews, sanitizers and trail restoration')
print(out / 'contact-sheet.png')
