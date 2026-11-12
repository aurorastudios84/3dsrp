#!/usr/bin/env python3
"""Exercise actual recvmsg timestamp, truncation and overflow collection on loopback."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.c').write_text(r'''
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include "receiver_probe.h"
static uint64_t stats[RRX_COUNT];
void relay_rx_add(unsigned k,uint64_t n){assert(k<RRX_COUNT);stats[k]+=n;}
void relay_rx_max(unsigned k,uint64_t n){assert(k<RRX_COUNT);if(n>stats[k])stats[k]=n;}
int main(void){
 int rx=socket(AF_INET,SOCK_DGRAM,0),tx=socket(AF_INET,SOCK_DGRAM,0);assert(rx>=0&&tx>=0);
 struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 assert(!bind(rx,(struct sockaddr*)&a,sizeof(a)));socklen_t alen=sizeof(a);assert(!getsockname(rx,(struct sockaddr*)&a,&alen));assert(!connect(tx,(struct sockaddr*)&a,sizeof(a)));
 RelayRxState state;relay_rx_setup(rx,&state);assert(stats[RRX_TIMESTAMP_ENABLED]==1&&stats[RRX_OVERFLOW_ENABLED]==1);
 uint8_t in[2000]={2},out[1500];assert(send(tx,in,12,0)==12);usleep(30000);
 assert(relay_rx_receive(rx,out,sizeof(out),&state,15,2)==12);assert(stats[RRX_STAMPED]==1&&stats[RRX_QUEUE_MAX_US]>=20000&&stats[RRX_QUEUE_20MS]==1);
 usleep(80000);assert(send(tx,in,12,0)==12);assert(relay_rx_receive(rx,out,sizeof(out),&state,15,2)==12);assert(stats[RRX_VIDEO_GAP_MAX_US]>=75000&&stats[RRX_VIDEO_GAP_75MS]==1);
 assert(send(tx,in,sizeof(in),0)==sizeof(in));assert(relay_rx_receive(rx,out,16,&state,15,2)==16);assert(stats[RRX_TRUNCATED]==1&&!stats[RRX_CONTROL_TRUNCATED]);
 int size=1024;assert(!setsockopt(rx,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size)));assert(!fcntl(rx,F_SETFL,O_NONBLOCK));
 for(int i=0;i<1000;i++)assert(send(tx,in,1000,0)==1000);
 while(relay_rx_receive(rx,out,sizeof(out),&state,15,2)>0){}
 assert(errno==EAGAIN||errno==EWOULDBLOCK);
 assert(send(tx,in,12,0)==12);assert(relay_rx_receive(rx,out,sizeof(out),&state,15,2)==12);assert(stats[RRX_KERNEL_DROPS]>0);
 uint64_t drops=stats[RRX_KERNEL_DROPS];assert(send(tx,in,12,0)==12);assert(relay_rx_receive(rx,out,sizeof(out),&state,15,2)==12);assert(stats[RRX_KERNEL_DROPS]==drops); // cumulative counter must not double-count
 close(rx);close(tx);
 // Unsupported/disabled timestamp options must not prevent normal reception.
 rx=socket(AF_INET,SOCK_DGRAM,0);tx=socket(AF_INET,SOCK_DGRAM,0);a.sin_port=0;assert(!bind(rx,(struct sockaddr*)&a,sizeof(a)));assert(!getsockname(rx,(struct sockaddr*)&a,&alen));assert(!connect(tx,(struct sockaddr*)&a,sizeof(a)));
 memset(&state,0,sizeof(state));uint64_t stamped=stats[RRX_STAMPED];assert(send(tx,in,12,0)==12);assert(relay_rx_receive(rx,out,sizeof(out),&state,15,2)==12);assert(stats[RRX_STAMPED]==stamped);close(rx);close(tx);
}
''')
 subprocess.run(['cc','-O2','-g','-fsanitize=address,undefined','-I'+str(root/'common'),p/'check.c','-o',p/'check'],check=True)
 subprocess.run([p/'check'],check=True)
print('PASS live UDP kernel timestamps, queue delay, arrival gaps, truncation, overflow deltas and no-timestamp fallback')
