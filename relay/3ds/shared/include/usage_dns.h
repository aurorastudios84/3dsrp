#ifndef THREEDSRP_USAGE_DNS_H
#define THREEDSRP_USAGE_DNS_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
/* Bounded DNS query/answer handling; returns a network-order IPv4 address. */
size_t usage_dns_query(uint8_t *out,size_t capacity,const char *host,uint16_t id);
bool usage_dns_answer(const uint8_t *data,size_t length,const uint8_t *query,size_t query_length,uint32_t *address);
#endif
