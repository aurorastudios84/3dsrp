#!/usr/bin/env python3
"""Verify pinned source, built core selection, pairing ABI and real upstream AV reordering."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
root=Path(__file__).resolve().parents[1];core=root/'vendor/chiaki-upstream';old=root.parent/'source/chiaki-ng';gen=root/'android/app/src/main/cpp/generated'
m=json.loads((core/'UPSTREAM.json').read_text())
assert m['commit']=='0c4a45df0cae2af2ba2daef84e881850b07038a3'
patches=json.loads((core/'LOCAL_PATCHES.json').read_text())['files']
assert set(patches)=={'lib/src/feedbacksender.c','lib/include/chiaki/feedbacksender.h','lib/src/ctrl.c'}
for name,digest in m['files'].items():
 if name in patches:
  assert patches[name]['upstream_sha256']==digest,name
  digest=patches[name]['patched_sha256']
 assert hashlib.sha256((core/name).read_bytes()).hexdigest()==digest,name
assert (core/'lib/protobuf/takion.proto').read_bytes()==(old/'lib/protobuf/takion.proto').read_bytes()
commands=max((root/'android/app/.cxx').rglob('compile_commands.json'),key=lambda p:p.stat().st_mtime)
c=json.loads(commands.read_text());sources=[x['file'] for x in c]
assert any('/chiaki-upstream/lib/src/takion.c' in f for f in sources)
assert not any('/source/chiaki-ng/lib/src/' in f for f in sources)
assert not any('CHIAKI_OLD3DS' in x['command'] or 'CHIAKI_ANDROID_RELAY' in x['command'] for x in c)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'pair.c').write_text('''#include <stdio.h>
#include <stddef.h>
#include <chiaki/regist.h>
struct Pair {uint32_t magic;char address[64];uint8_t account[8];ChiakiRegisteredHost host;};
int main(void){printf("%zu %zu %zu %zu %zu",sizeof(struct Pair),offsetof(struct Pair,host),sizeof(ChiakiRegisteredHost),offsetof(ChiakiRegisteredHost,rp_regist_key),offsetof(ChiakiRegisteredHost,rp_key));}''')
 layouts=[]
 for src in [old,core]:
  subprocess.run(['cc','-I'+str(src/'lib/include'),'-I'+str(gen),'-I'+str(root/'common'),p/'pair.c','-o',p/'pair'],check=True);layouts.append(subprocess.check_output([p/'pair']))
 assert layouts[0]==layouts[1],layouts
 (p/'queue.c').write_text(r'''
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include "TAKION"
static unsigned delivered;static uint16_t order[8];static uint64_t now=1000;
uint64_t chiaki_time_now_monotonic_us(void){return now;}
void chiaki_log(ChiakiLog *l,ChiakiLogLevel v,const char *fmt,...){}
static ChiakiErrorCode parse(ChiakiTakionAVPacket *p,ChiakiKeyState *k,uint8_t *b,size_t n){assert(n==4);memset(p,0,sizeof(*p));p->packet_index=(b[0]<<8)|b[1];p->unit_index=b[2];p->is_video=true;p->data=b;p->data_size=n;return CHIAKI_ERR_SUCCESS;}
static void cb(ChiakiTakionEvent *e,void *u){assert(e->type==CHIAKI_TAKION_EVENT_TYPE_AV&&delivered<8);order[delivered++]=e->av->packet_index;}
static void push(ChiakiTakion *t,uint16_t index,unsigned unit){uint8_t *b=malloc(4);b[0]=index>>8;b[1]=index;b[2]=unit;b[3]=0;takion_handle_packet_av(t,TAKION_PACKET_TYPE_VIDEO,b,4);}
int main(void){ChiakiTakion t={0};t.cb=cb;t.av_packet_parse=parse;
 push(&t,0,0);assert(delivered==1&&order[0]==0);
 push(&t,2,2);assert(delivered==1);push(&t,1,1);assert(delivered==3&&order[1]==1&&order[2]==2);
 push(&t,4,4);assert(delivered==3);now+=100000;takion_av_queues_flush_with_timeout(&t);assert(delivered==4&&order[3]==4);
 chiaki_reorder_queue_fini(&t.video_queue);
}
'''.replace('TAKION',str(core/'lib/src/takion.c'),1))
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(core/'lib/include'),'-I'+str(gen),'-I'+str(old/'third-party/nanopb'),p/'queue.c',core/'lib/src/reorderqueue.c','-o',p/'queue'],check=True)
 subprocess.run([p/'queue'],check=True)
print('PASS upstream pin and explicit local release-retry patch, build selection, compatible saved pairing, AV reorder and timeout recovery')
