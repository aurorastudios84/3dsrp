// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/*
 * Build-time compatibility shims for the Nintendo 3DS build.
 *
 * devkitARM's newlib + libctru give us IPv4 sockets only: there is no
 * struct sockaddr_in6, no INET6_ADDRSTRLEN, no in6addr_any. chiaki's own
 * headers and inline helpers still name them (utils.h's set_port/sockaddr_str,
 * ctrl.c's session address, remote/holepunch.h's regist_local_ip field), so the
 * missing pieces are supplied here.
 *
 * Nothing on the PS5 Remote Play path uses IPv6 on this port -- the 3DS SOC is
 * IPv4 -- so the branches that dereference these types are never taken at
 * runtime; this file exists so the code compiles and the IPv4 path links.
 *
 * lib/CMakeLists.txt force-includes this header (-include) into every
 * chiaki-lib translation unit when CHIAKI_IS_3DS is set.
 */
#ifndef CHIAKI_3DS_COMPAT_H
#define CHIAKI_3DS_COMPAT_H

#include <stdint.h>

#include <sys/socket.h>
#include <netinet/in.h>
/* On 3DS htonl/ntohl/htons/inet_ntop live here, not in netinet/in.h
 * (upstream only pulls this in for __SWITCH__, for the same reason). */
#include <arpa/inet.h>

/* libctru's netinet/in.h is IPv4-only. */
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

#ifndef AF_INET6
#define AF_INET6 28
#endif

struct in6_addr
{
	uint8_t s6_addr[16];
};

struct sockaddr_in6
{
	sa_family_t     sin6_family;
	in_port_t       sin6_port;
	uint32_t        sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t        sin6_scope_id;
};

#define IN6ADDR_ANY_INIT { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }

static const struct in6_addr chiaki_3ds_in6addr_any __attribute__((unused)) = IN6ADDR_ANY_INIT;
#define in6addr_any chiaki_3ds_in6addr_any

#include <chiaki/io_3ds.h>
#define socket chiaki_io_socket
#define connect chiaki_io_connect
#define select chiaki_io_select
#define send chiaki_io_send
#define sendto chiaki_io_sendto
#define recv chiaki_io_recv
#define recvfrom chiaki_io_recvfrom
#define close chiaki_io_close

#endif
