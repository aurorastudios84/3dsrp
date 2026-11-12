#pragma once
#include "usage_stats.h"
#include <stddef.h>
typedef void (*UsageRelaySend)(unsigned,const uint8_t *,unsigned,uint32_t);
void usage_relay_init(uint32_t run,UsageRelaySend send);
bool usage_relay_unasked(void);
bool usage_relay_allowed(void);
bool usage_relay_choose(bool allow);
void usage_relay_receive(const uint8_t *packet,size_t size);
void usage_relay_frame(uint32_t session,uint32_t now);
void usage_relay_tick(bool connected,bool live,uint32_t now);
void usage_relay_suspend(void);
void usage_relay_shutdown(bool connected);
