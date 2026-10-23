#ifndef N3DS_MVD_SINK_H
#define N3DS_MVD_SINK_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
int mvd_sink_start(unsigned width,unsigned height,unsigned h264_level);

void mvd_sink_stop(void);
void mvd_sink_cancel(void);
void mvd_sink_reset_cancel(void);
typedef enum {
    MVD_FEED_CONSUMED_EMPTY=0,
    MVD_FEED_CONSUMED_FRAME=1,
    MVD_FEED_RETRY_EMPTY=2,
    MVD_FEED_RETRY_FRAME=3
} MvdFeedResult;
/* Negative values are decoder errors. RETRY leaves the same AU owned by the
 * caller; any FRAME result must be copied before calling the sink again. */
int mvd_sink_feed_au(const uint8_t *data,size_t size,bool decode_picture);
bool mvd_sink_copy_source(uint16_t *destination,size_t capacity_pixels,
                          unsigned *width,unsigned *height);
void mvd_sink_copy_frame(uint16_t *framebuffer);
#endif
