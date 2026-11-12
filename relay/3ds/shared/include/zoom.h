#ifndef N3DS_ZOOM_H
#define N3DS_ZOOM_H
#include <stdbool.h>
#include <stdint.h>
#define ZOOM_MIN_Q 6u
#define ZOOM_MAX_Q 16u
#define ZOOM_DEFAULT_Q 8u
#define ZOOM_CENTER 32768u
#define ZOOM_BOTTOM_PIXELS (320u * 240u)
/* UI-owned normalized centre and quarter-unit magnification relative to top. */
typedef struct { uint16_t center_x, center_y; uint8_t zoom_q; } ZoomView;
typedef enum { ZOOM_LABEL_NONE, ZOOM_LABEL_READING, ZOOM_LABEL_HUD } ZoomLabel;
typedef struct { ZoomView view; bool box; uint32_t revision; ZoomLabel mode_label; } ZoomPresentation;
typedef struct { unsigned x, y, width, height; } ZoomRect;
/* Pure geometry; false for unsupported/invalid source or scale. */
bool zoom_crop_rect(unsigned width, unsigned height, const ZoomView *view, ZoomRect *crop);
/* Pure source-to-framebuffer mapping. Packed/strided source RGB565; output
 * rotated column-major RGB565. Optional top/bottom outputs. No allocations.
 * Both views use the same source; false for invalid arguments/geometry. */
bool zoom_render_frame(const uint16_t *source, unsigned width, unsigned height,
                       unsigned stride, const ZoomPresentation *presentation,
                       uint16_t *top, uint16_t *bottom);
/* Draw only the existing crop outline/badge onto a caller-initialized plane.
 * Pixels outside these overlays are left untouched (also permits transparency). */
bool zoom_render_top_overlay(unsigned width,unsigned height,
                             const ZoomPresentation *presentation,uint16_t *top);
#endif
