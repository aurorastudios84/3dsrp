#ifndef N3DS_TEXTURE_PACK_H
#define N3DS_TEXTURE_PACK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* CPU upload only: packs RGB565 into PICA 8x8 Morton tiles, without resizing.
 * Replicates the visible edge into a one-texel sampling margin. */
bool rgb565_texture_pack(uint16_t *texture, size_t capacity, unsigned tw, unsigned th,
                     const uint16_t *source, unsigned width, unsigned height,
                     unsigned stride);
#endif
