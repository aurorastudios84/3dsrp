#pragma once
#include "wire.h"
#define RELAY_FEEDBACK_BYTES 48u
/* Cumulative counters are subtracted modulo 2^32 once per reporting window. */
typedef struct { uint32_t decoded,decode_us,shown,pack_us,received,missing,age_us,controls; } RelayVideoCounters;
static inline void relay_video_feedback(uint8_t out[RELAY_FEEDBACK_BYTES],
 uint32_t elapsed_ms,const RelayVideoCounters *now,RelayVideoCounters *previous,uint32_t max_age_us){
 w32(out,1);w32(out+4,elapsed_ms);
 w32(out+8,now->decoded-previous->decoded);w32(out+12,now->decode_us-previous->decode_us);
 w32(out+16,now->shown-previous->shown);w32(out+20,now->pack_us-previous->pack_us);
 w32(out+24,now->received-previous->received);w32(out+28,now->missing-previous->missing);
 w32(out+32,now->age_us-previous->age_us);w32(out+36,now->shown-previous->shown);
 w32(out+40,max_age_us);w32(out+44,now->controls-previous->controls);
 *previous=*now;
}
