#include <chiaki/io_3ds.h>
#include <chiaki/time.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#ifdef __3DS__
#include <3ds.h>
#endif

#undef socket
#undef connect
#undef select
#undef send
#undef sendto
#undef recv
#undef recvfrom
#undef close

static void yield_io(void)
{
#ifdef __3DS__
    svcSleepThread(2000000);
#else
    usleep(2000);
#endif
}

static bool abort_io(uint32_t start, uint32_t limit)
{
    if(limit != UINT32_MAX && (uint32_t)chiaki_time_now_monotonic_ms()-start >= limit) { errno=ETIMEDOUT; return true; }
    return false;
}

int chiaki_io_socket(int domain, int type, int protocol)
{
    int result=socket(domain,type,protocol);
    if(result >= 0 && fcntl(result,F_SETFL,O_NONBLOCK) < 0) {
        int saved=errno; close(result); errno=saved; result=-1;
    }

    return result;
}

int chiaki_io_connect(int descriptor, const struct sockaddr *address, socklen_t length)
{
    uint32_t started=(uint32_t)chiaki_time_now_monotonic_ms();
    int result=-1;
    if(!abort_io(started,UINT32_MAX)) result=connect(descriptor,address,length);

    return result;
}

int chiaki_io_select(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set, struct timeval *timeout)
{
    uint64_t requested=timeout ? (uint64_t)timeout->tv_sec*1000 + (timeout->tv_usec+999)/1000 : UINT32_MAX;
    uint32_t limit=requested > UINT32_MAX ? UINT32_MAX : (uint32_t)requested;
    uint32_t started=(uint32_t)chiaki_time_now_monotonic_ms();
    int result=-1;
    for(;;) {
        fd_set reads, writes, errors;
        if(read_set) reads=*read_set;
        if(write_set) writes=*write_set;
        if(error_set) errors=*error_set;
        struct timeval immediate={0};

        result=select(count,read_set ? &reads : NULL,write_set ? &writes : NULL,error_set ? &errors : NULL,&immediate);

        if(result != 0 || (limit != UINT32_MAX && (uint32_t)chiaki_time_now_monotonic_ms()-started >= limit)) {
            if(read_set) *read_set=reads;
            if(write_set) *write_set=writes;
            if(error_set) *error_set=errors;
            break;
        }
        yield_io();
    }

    return result;
}

ssize_t chiaki_io_sendto(int descriptor, const void *buffer, size_t size, int flags, const struct sockaddr *address, socklen_t length)
{
    uint32_t started=(uint32_t)chiaki_time_now_monotonic_ms();
    ssize_t result=-1;
    for(;;) {
        if(abort_io(started,3000)) { result=-1; break; }
        result=sendto(descriptor,buffer,size,flags,address,length);
        if(result >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) break;
        yield_io();
    }

    return result;
}

ssize_t chiaki_io_send(int descriptor, const void *buffer, size_t size, int flags)
{
    return chiaki_io_sendto(descriptor,buffer,size,flags,NULL,0);
}

ssize_t chiaki_io_recvfrom(int descriptor, void *buffer, size_t size, int flags, struct sockaddr *address, socklen_t *length)
{
    uint32_t started=(uint32_t)chiaki_time_now_monotonic_ms();
    ssize_t result=-1;
    if(!abort_io(started,UINT32_MAX)) result=recvfrom(descriptor,buffer,size,flags,address,length);

    return result;
}

ssize_t chiaki_io_recv(int descriptor, void *buffer, size_t size, int flags)
{
    return chiaki_io_recvfrom(descriptor,buffer,size,flags,NULL,NULL);
}

int chiaki_io_close(int descriptor)
{
    int result=close(descriptor);

    return result;
}
