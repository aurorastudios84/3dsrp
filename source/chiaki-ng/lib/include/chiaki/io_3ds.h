#ifndef CHIAKI_IO_3DS_H
#define CHIAKI_IO_3DS_H
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

int chiaki_io_socket(int domain, int type, int protocol);
int chiaki_io_connect(int descriptor, const struct sockaddr *address, socklen_t length);
int chiaki_io_select(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set, struct timeval *timeout);
ssize_t chiaki_io_send(int descriptor, const void *buffer, size_t size, int flags);
ssize_t chiaki_io_sendto(int descriptor, const void *buffer, size_t size, int flags, const struct sockaddr *address, socklen_t length);
ssize_t chiaki_io_recv(int descriptor, void *buffer, size_t size, int flags);
ssize_t chiaki_io_recvfrom(int descriptor, void *buffer, size_t size, int flags, struct sockaddr *address, socklen_t *length);
int chiaki_io_close(int descriptor);
#endif
