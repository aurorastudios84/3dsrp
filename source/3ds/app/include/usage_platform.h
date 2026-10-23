#ifndef THREEDSRP_USAGE_PLATFORM_H
#define THREEDSRP_USAGE_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
uint32_t usage_platform_millis(void);
int64_t usage_platform_epoch(void);
bool usage_platform_dns(uint32_t *network_address,uint16_t *port);
int usage_platform_random(void *unused,unsigned char *out,size_t size);
#endif
