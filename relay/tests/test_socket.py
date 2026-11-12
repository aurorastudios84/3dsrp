#!/usr/bin/env python3
"""Check the actual explicit-bind socket helper and receiver startup contract."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='relay-socket-test-') as tmp:
 work=Path(tmp)
 (work/'test.c').write_text(r'''
#include "net_socket.h"
#include <assert.h>
#include <arpa/inet.h>
#include <string.h>
int main(void){
 RelaySocketSetup setup;int fd=relay_open_socket(&setup);uint16_t port=setup.local_port;assert(fd>=0&&port&&setup.error==0);
 assert(fcntl(fd,F_GETFL,0)&O_NONBLOCK);
 char bytes[8];struct sockaddr_in from={0};socklen_t len=sizeof(from);
 assert(recvfrom(fd,bytes,sizeof(bytes),0,(struct sockaddr*)&from,&len)==-1);
 assert(errno==EAGAIN||errno==EWOULDBLOCK); /* receive before first send */
 int server=socket(AF_INET,SOCK_DGRAM,0);assert(server>=0);
 struct sockaddr_in local={0};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);local.sin_port=htons(port);
 assert(sendto(server,"hello",5,0,(struct sockaddr*)&local,sizeof(local))==5);
 for(unsigned i=0;i<100;i++){
  len=sizeof(from);int n=recvfrom(fd,bytes,sizeof(bytes),0,(struct sockaddr*)&from,&len);
  if(n==5){assert(!memcmp(bytes,"hello",5));close(server);close(fd);return 0;}
  assert(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK));usleep(1000);
 }
 assert(!"bound receiver failed before any outbound message");
}
''')
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'common'),work/'test.c','-o',work/'test'],check=True)
 subprocess.run([work/'test'],check=True)
# Exercise the actual helper against SOC-like argument restrictions/errors.
with tempfile.TemporaryDirectory(prefix='relay-socket-compat-') as tmp:
 work=Path(tmp)
 (work/'test.c').write_text(r'''
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
static int mode,binds,closed;static uint16_t bound;
static int fake_socket(int family,int type,int protocol){assert(family==AF_INET&&type==SOCK_DGRAM&&protocol==IPPROTO_UDP);if(mode==4){errno=ENFILE;return -1;}return 7;}
static int fake_bind(int fd,const struct sockaddr *address,socklen_t size){
 assert(fd==7&&size==sizeof(struct sockaddr_in));const struct sockaddr_in *p=(const void*)address;binds++;
 if(mode==1||(mode==0&&p->sin_port==0)){errno=EINVAL;return -1;}
 bound=p->sin_port?p->sin_port:htons(43210);return 0;
}
static int fake_fcntl(int fd,int command,...){
 assert(fd==7&&command==F_SETFL);va_list args;va_start(args,command);assert(va_arg(args,int)==O_NONBLOCK);va_end(args);
 if(mode==2){errno=EINVAL;return -1;}return 0;
}
static int fake_name(int fd,struct sockaddr *address,socklen_t *size){
 assert(fd==7);if(mode==3){errno=ENOSYS;return -1;}
 struct sockaddr_in *p=(void*)address;memset(p,0,sizeof(*p));p->sin_family=AF_INET;p->sin_port=bound;*size=sizeof(*p);return 0;
}
static int fake_close(int fd){assert(fd==7);closed++;return 0;}
#define socket fake_socket
#define bind fake_bind
#define fcntl fake_fcntl
#define getsockname fake_name
#define close fake_close
#include "net_socket.h"
int main(void){
 for(mode=0;mode<6;mode++){
  binds=closed=0;RelaySocketSetup s;int fd=relay_open_socket(&s);
  if(mode==0){assert(fd==7&&binds==2&&!closed&&s.local_port==29871&&s.automatic_bind_error==EINVAL);}
  if(mode==1){assert(fd<0&&closed==1&&s.error==EINVAL&&!strcmp(s.step,"bind"));}
  if(mode==2){assert(fd<0&&closed==1&&s.error==EINVAL&&!strcmp(s.step,"nonblocking"));}
  if(mode==3){assert(fd==7&&!closed&&s.port_query_error==ENOSYS&&!strcmp(s.step,"ready"));}
  if(mode==4){assert(fd<0&&!closed&&s.error==ENFILE&&!strcmp(s.step,"create"));}
  if(mode==5){assert(fd==7&&binds==1&&!closed&&s.local_port==43210&&!s.automatic_bind_error);}
 }
}
''')
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'common'),work/'test.c','-o',work/'test'],check=True)
 subprocess.run([work/'test'],check=True)
print('PASS SOC-style EINVAL automatic-bind fallback, supported nonblocking flags, optional port query and named failures/cleanup')
source=(ROOT/'3ds/main.c').read_text()
assert source.index('sock=relay_open_socket(')<source.index('receiver=threadCreate(')
assert 'atomic_store(&receive_errno,errno)' in source and 'if(errno==EINTR)continue' in source
assert source.count('GSPGPU_FlushDataCache(gfxGetFramebuffer(GFX_BOTTOM')>=3
print('PASS actual bound nonblocking UDP receiver accepts inbound packets before first send; bind-before-worker and visible console/error paths')
