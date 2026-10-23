#include "usage_dns.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
size_t usage_dns_query(uint8_t *out,size_t capacity,const char *host,uint16_t id)
{
    if(capacity<18 || !host || !host[0]) return 0;
    memset(out,0,12);out[0]=(uint8_t)(id>>8);out[1]=(uint8_t)id;out[2]=1;out[5]=1;
    size_t n=12;
    while(*host) {
        const char *end=strchr(host,'.');size_t len=end ? (size_t)(end-host) : strlen(host);
        if(!len || len>63 || n+len+6>capacity) return 0;
        out[n++]=(uint8_t)len;memcpy(out+n,host,len);n+=len;
        host+=len;if(*host=='.')host++;
    }
    out[n++]=0;out[n++]=0;out[n++]=1;out[n++]=0;out[n++]=1;return n;
}
static bool skip_name(const uint8_t *p,size_t len,size_t *position)
{
    size_t n=*position;
    for(unsigned labels=0;labels<128;labels++) {
        if(n>=len)return false;
        unsigned size=p[n++];
        if(!size){*position=n;return true;}
        if((size&192u)==192u) {
            if(n>=len || (((size&63u)<<8)|p[n])>=len)return false;
            *position=n+1;return true;
        }
        if(size>63 || n+size>len)return false;
        n+=size;
    }
    return false;
}
bool usage_dns_answer(const uint8_t *p,size_t len,const uint8_t *query,size_t qlen,uint32_t *address)
{
    if(len<12 || qlen<17 || len<qlen || memcmp(p,query,2) || !(p[2]&128u) ||
       (p[2]&122u) || (p[3]&15u) || u16(p+4)!=1 || memcmp(p+12,query+12,qlen-12))return false;
    size_t n=qlen;unsigned answers=u16(p+6);
    for(unsigned i=0;i<answers;i++) {
        if(!skip_name(p,len,&n) || n+10>len)return false;
        unsigned type=u16(p+n),klass=u16(p+n+2),size=u16(p+n+8);n+=10;
        if(size>len-n)return false;
        if(type==1 && klass==1 && size==4){memcpy(address,p+n,4);return *address!=0;}
        n+=size;
    }
    return false;
}
