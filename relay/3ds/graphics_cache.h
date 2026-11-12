#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t frames,geometry_flush_bytes,full_heap_fallbacks;
} BetaGraphicsStats;
bool beta_graphics_init(void);
void beta_graphics_frame_end(void);
/* Main-thread-only counters; command-list bytes are deliberately excluded. */
BetaGraphicsStats beta_graphics_stats(void);
void beta_graphics_stats_reset(void);
/* Internal hooks shared with the serialized linear allocator. */
void beta_graphics_capture_begin(void);
void beta_graphics_capture_end(bool initialized);
bool beta_graphics_flush_buffers(size_t *flushed_bytes);
