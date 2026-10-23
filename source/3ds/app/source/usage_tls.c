/* The private restartable library avoids changing Chiaki's crypto ABI. */
#define USAGE_PRIVATE_TLS_BUILD
#include "../../../vendor/usage-mbedtls/include/mbedtls/config.h"
#include "usage_mbedtls_symbols.h"
#include "usage_stats.h"
#include "usage_config.h"
#include "usage_roots.h"
#include "usage_dns.h"
#include "usage_platform.h"
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/net_sockets.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* All sockets are nonblocking. No getaddrinfo, synchronous TLS handshake,
 * sender thread, network wait, retry or plaintext-report fallback. */
enum { DNS_SEND,DNS_READ,TCP_CONNECT,TLS_HANDSHAKE,HTTP_WRITE,HTTP_READ };
static struct {
    mbedtls_ssl_context ssl;mbedtls_ssl_config config;
    mbedtls_x509_crt roots;mbedtls_ctr_drbg_context rng;
    int dns_socket,tcp_socket,state;bool initialized;
    char host[128],path[128],request[512],response[128];uint16_t port;
    int last_error,socket_error;uint32_t address;unsigned crypto_yields;
    uint8_t query[256];size_t query_size,request_size,written,received;
} net;
static bool would_block(void){return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINPROGRESS;}
static int nonblocking_socket(int type)
{
    int fd=socket(AF_INET,type,0);if(fd<0)return -1;
    int flags=fcntl(fd,F_GETFL,0);
    if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return -1;}return fd;
}
static void cancel(void)
{
    if(net.dns_socket>=0){close(net.dns_socket);net.dns_socket=-1;}
    if(net.tcp_socket>=0){close(net.tcp_socket);net.tcp_socket=-1;}
}
static void cleanup(void)
{
    cancel();
    if(net.initialized){mbedtls_ssl_free(&net.ssl);mbedtls_ssl_config_free(&net.config);mbedtls_x509_crt_free(&net.roots);mbedtls_ctr_drbg_free(&net.rng);net.initialized=false;}
}
static bool init(void)
{
    memset(&net,0,sizeof(net));net.dns_socket=net.tcp_socket=-1;
    mbedtls_ssl_init(&net.ssl);mbedtls_ssl_config_init(&net.config);mbedtls_x509_crt_init(&net.roots);mbedtls_ctr_drbg_init(&net.rng);net.initialized=true;
    mbedtls_ecp_set_max_ops(400);
    static const unsigned char personal[]="3DSRP optional statistics";
    if(mbedtls_ctr_drbg_seed(&net.rng,usage_platform_random,NULL,personal,sizeof(personal)-1)){cleanup();return false;}return true;
}
static bool open_context(void)
{
    const char *url=USAGE_ENDPOINT;
    if(strncmp(url,"https://",8))return false;
    url+=8;const char *slash=strchr(url,'/');if(!slash)return false;
    size_t host_len=(size_t)(slash-url);if(!host_len || host_len>=sizeof(net.host) || strlen(slash)>=sizeof(net.path))return false;
    memcpy(net.host,url,host_len);strcpy(net.path,slash);net.port=443;
    char *colon=strchr(net.host,':');
    if(colon){*colon++=0;char *end;unsigned long port=strtoul(colon,&end,10);if(*end || !port || port>65535)return false;net.port=(uint16_t)port;}
    uint32_t dns_address;uint16_t dns_port;if(!usage_platform_dns(&dns_address,&dns_port))return false;
    net.dns_socket=nonblocking_socket(SOCK_DGRAM);if(net.dns_socket<0)return false;
    struct sockaddr_in dns={.sin_family=AF_INET,.sin_port=htons(dns_port)};dns.sin_addr.s_addr=dns_address;
    if(connect(net.dns_socket,(struct sockaddr*)&dns,sizeof(dns))<0 && !would_block())return false;
    uint8_t id[2];if(mbedtls_ctr_drbg_random(&net.rng,id,sizeof(id)))return false;
    net.query_size=usage_dns_query(net.query,sizeof(net.query),net.host,(uint16_t)((unsigned)id[0]<<8|id[1]));
    return net.query_size!=0;
}
static bool trust(void)
{
    for(unsigned i=0;i<USAGE_ROOT_COUNT;i++)
        if(mbedtls_x509_crt_parse_der(&net.roots,usage_roots[i].bytes,usage_roots[i].size))return false;
    return true;
}
static int send_tls(void *unused,const unsigned char *p,size_t size)
{
    (void)unused;int n=send(net.tcp_socket,p,size,0);
    if(n<0 && !would_block())net.socket_error=errno;
    return n>=0?n:would_block()?MBEDTLS_ERR_SSL_WANT_WRITE:MBEDTLS_ERR_NET_SEND_FAILED;
}
static int receive_tls(void *unused,unsigned char *p,size_t size)
{
    (void)unused;int n=recv(net.tcp_socket,p,size,0);
    if(n<0 && !would_block())net.socket_error=errno;
    return n>=0?n:would_block()?MBEDTLS_ERR_SSL_WANT_READ:MBEDTLS_ERR_NET_RECV_FAILED;
}
static bool headers(void)
{
    if(mbedtls_ssl_config_defaults(&net.config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT))return false;
    /* Restartable TLS supports ECDHE-ECDSA. Keep the negotiated work bounded. */
    static const int ciphers[]={MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,0};
    mbedtls_ssl_conf_ciphersuites(&net.config,ciphers);
    mbedtls_ssl_conf_authmode(&net.config,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&net.config,&net.roots,NULL);
    mbedtls_ssl_conf_rng(&net.config,mbedtls_ctr_drbg_random,&net.rng);
    mbedtls_ssl_conf_min_version(&net.config,MBEDTLS_SSL_MAJOR_VERSION_3,MBEDTLS_SSL_MINOR_VERSION_3);
    if(mbedtls_ssl_setup(&net.ssl,&net.config) || mbedtls_ssl_set_hostname(&net.ssl,net.host))return false;
    mbedtls_ssl_set_bio(&net.ssl,NULL,send_tls,receive_tls,NULL);return true;
}
static bool body(const char *uuid,const UsageReport *report)
{
    char json[256];int n=snprintf(json,sizeof(json),"{\"installation_id\":\"%s\",\"session_id\":\"%s\",\"day\":\"%s\",\"seconds\":%lu}",uuid,report->session_id,report->day,(unsigned long)(report->milliseconds/1000u));
    if(n<0 || (size_t)n>=sizeof(json))return false;
    int size=snprintf(net.request,sizeof(net.request),"POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: 3DSRP\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",net.path,net.host,n,json);
    if(size<0 || (size_t)size>=sizeof(net.request))return false;
    net.request_size=(size_t)size;return true;
}
static bool begin_async(void){net.state=DNS_SEND;return true;}
static int tcp_start(uint32_t address)
{
    net.address=address;close(net.dns_socket);net.dns_socket=-1;
    net.tcp_socket=nonblocking_socket(SOCK_STREAM);if(net.tcp_socket<0)return -1;
    struct sockaddr_in server={.sin_family=AF_INET,.sin_port=htons(net.port)};server.sin_addr.s_addr=address;
    int r=connect(net.tcp_socket,(struct sockaddr*)&server,sizeof(server));
    if(r<0 && !would_block())return -1;
    net.state=r==0?TLS_HANDSHAKE:TCP_CONNECT;return 0;
}
static int poll_request(void)
{
    int r;
    switch(net.state){
    case DNS_SEND:
        r=send(net.dns_socket,net.query,net.query_size,0);
        if(r==(int)net.query_size){net.state=DNS_READ;return 0;}return r<0&&would_block()?0:-1;
    case DNS_READ:{
        uint8_t packet[512];uint32_t address;r=recv(net.dns_socket,packet,sizeof(packet),0);
        if(r<0)return would_block()?0:-1;
        if(!usage_dns_answer(packet,(size_t)r,net.query,net.query_size,&address))return -1;
        return tcp_start(address);
    }
    case TCP_CONNECT:{
        fd_set writefds;FD_ZERO(&writefds);FD_SET(net.tcp_socket,&writefds);struct timeval zero={0};
        r=select(net.tcp_socket+1,NULL,&writefds,NULL,&zero);if(r<0)return -1;if(!r)return 0;
        /* SOC SO_ERROR values are not consistently POSIX-normalized. A
         * writable socket may still be completing; nonblocking TLS send/recv
         * will report WANT_* or the actual connection failure without waiting. */
        net.state=TLS_HANDSHAKE;return 0;
    }
    case TLS_HANDSHAKE:{
        uint32_t started=usage_platform_millis();
        for(unsigned steps=0;steps<64;steps++) {
            r=mbedtls_ssl_handshake_step(&net.ssl);
            if(r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE)return 0;
            if(r==MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS)net.crypto_yields++;
            if(r && r!=MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS){net.last_error=r;return -1;}
            if(net.ssl.state==MBEDTLS_SSL_HANDSHAKE_OVER){if(mbedtls_ssl_get_verify_result(&net.ssl))return -1;net.state=HTTP_WRITE;return 0;}
            if((uint32_t)(usage_platform_millis()-started)>=4)return 0;
        }
        return 0;
    }
    case HTTP_WRITE:
        r=mbedtls_ssl_write(&net.ssl,(const unsigned char*)net.request+net.written,net.request_size-net.written);
        if(r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE)return 0;
        if(r<=0)return -1;
        net.written+=(size_t)r;if(net.written==net.request_size)net.state=HTTP_READ;return 0;
    case HTTP_READ:
        r=mbedtls_ssl_read(&net.ssl,(unsigned char*)net.response+net.received,sizeof(net.response)-net.received-1);
        if(r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE)return 0;
        if(r<=0)return -1;
        net.received+=(size_t)r;net.response[net.received]=0;
        if(strstr(net.response,"\r\n")){
            if(strncmp(net.response,"HTTP/1.1 ",9)&&strncmp(net.response,"HTTP/1.0 ",9))return -1;
            if(!strncmp(net.response+9,"204 ",4))return 1;
            if(!strncmp(net.response+9,"400 ",4)||!strncmp(net.response+9,"410 ",4))return 2;
            return -1;
        }
        return net.received==sizeof(net.response)-1?-1:0;
    default:return -1;
    }
}
const UsageHttpOps usage_http_3ds={init,open_context,trust,headers,body,begin_async,poll_request,cancel,cancel,cleanup};

#ifdef USAGE_TLS_TEST
unsigned usage_tls_test_yields(void){return net.crypto_yields;}
int usage_tls_test_stage(void){return net.state;}
int usage_tls_test_error(void){return net.last_error;}
int usage_tls_test_socket_error(void){return net.socket_error;}
uint32_t usage_tls_test_address(void){return net.address;}
#endif
