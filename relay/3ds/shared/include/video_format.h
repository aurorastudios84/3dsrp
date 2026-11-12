#ifndef N3DS_VIDEO_FORMAT_H
#define N3DS_VIDEO_FORMAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define VIDEO_MAX_AU (1024u * 1024u)
#define VIDEO_MAX_NALS 64u
typedef struct {
    size_t offset, size, prefix;
    unsigned type;
} VideoNal;
typedef struct {
    VideoNal nals[VIDEO_MAX_NALS];
    unsigned count;
    bool picture, idr, sps, pps;
} VideoAccessUnit;
typedef struct {
    unsigned profile, level, refs, width, height, coded_width, coded_height;
    unsigned crop_left,crop_top,crop_right,crop_bottom;
    uint32_t macroblocks, dpb_bytes;
} VideoSps;
bool video_parse_au(const uint8_t *data, size_t size, VideoAccessUnit *unit);
bool video_parse_sps(const uint8_t *data, size_t size, VideoSps *sps);
bool video_sps_safe(const VideoSps *sps, unsigned width, unsigned height,
                    uint32_t max_mbs, uint32_t max_dpb, uint32_t max_level);
#endif
