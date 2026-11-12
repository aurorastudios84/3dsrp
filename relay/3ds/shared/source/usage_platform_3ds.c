#include "usage_stats.h"
#include "usage_platform.h"
#include <3ds.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>
int64_t usage_platform_epoch(void){return (int64_t)time(NULL);}
uint32_t usage_platform_millis(void){return (uint32_t)(svcGetSystemTick()/268123.48);}
bool usage_platform_dns(uint32_t *address,uint16_t *port)
{
    SOCU_DNSTableEntry entries[32]={0};socklen_t size=sizeof(entries);
    if(SOCU_GetNetworkOpt(SOL_CONFIG,NETOPT_DNS_TABLE,entries,&size)<0)return false;
    for(unsigned i=0;i<size/sizeof(entries[0]) && i<32;i++)
        if(entries[i].family==AF_INET && entries[i].ip.s_addr){*address=entries[i].ip.s_addr;*port=53;return true;}
    return false;
}
int usage_platform_random(void *unused,unsigned char *out,size_t size)
{
    (void)unused;if(R_FAILED(psInit()))return -1;
    Result r=PS_GenerateRandomBytes(out,(u32)size);psExit();return R_SUCCEEDED(r)?0:-1;
}
bool usage_random_uuid(char uuid[37])
{
    uint8_t random[16];if(usage_platform_random(NULL,random,sizeof(random)))return false;
    usage_uuid_from_random(uuid,random);memset(random,0,sizeof(random));return true;
}
