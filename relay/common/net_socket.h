#pragma once
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef struct {
 const char *step;
 int error,automatic_bind_error,port_query_error,receive_buffer_error;
 uint16_t local_port;
} RelaySocketSetup;
/* Use libctru's supported F_SETFL form. F_GETFL may expose flags its
 * F_SETFL implementation refuses; a fresh UDP socket needs only O_NONBLOCK.
 * Port discovery is diagnostic, never a reason to discard a valid socket. */
static inline int relay_open_socket(RelaySocketSetup *setup){
 *setup=(RelaySocketSetup){.step="create"};
 int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(fd<0){setup->error=errno;return -1;}
 /* Allow a short picture burst using the modest receive-window request
  * already used by the original-3DS Remote Play core. Never fail startup
  * just because this optional buffer setting is unavailable. */
 int buffer_bytes=16*1024;
 if(setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&buffer_bytes,sizeof(buffer_bytes))<0)setup->receive_buffer_error=errno;
 struct sockaddr_in local={0};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_ANY);local.sin_port=0;
 setup->step="bind";
 if(bind(fd,(struct sockaddr*)&local,sizeof(local))<0){
  setup->automatic_bind_error=errno;
  /* SOC versions may reject explicit automatic-port binding. The phone
   * learns our source endpoint, so a regular high client port works too. */
  local.sin_port=htons(29871);
  if(bind(fd,(struct sockaddr*)&local,sizeof(local))<0)goto fail;
  setup->local_port=29871;
 }
 setup->step="nonblocking";
 if(fcntl(fd,F_SETFL,O_NONBLOCK)<0)goto fail;
 socklen_t size=sizeof(local);
 if(getsockname(fd,(struct sockaddr*)&local,&size)==0)setup->local_port=ntohs(local.sin_port);
 else setup->port_query_error=errno;
 setup->step="ready";return fd;
fail: {int saved=errno;setup->error=saved;close(fd);errno=saved;return -1;}
}
