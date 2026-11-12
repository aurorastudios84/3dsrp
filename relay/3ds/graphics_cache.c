#include "graphics_cache.h"
#include <3ds.h>
#include <citro2d.h>
static BetaGraphicsStats stats;
BetaGraphicsStats beta_graphics_stats(void) {return stats;}
void beta_graphics_stats_reset(void) {stats=(BetaGraphicsStats){0};}

bool beta_graphics_init(void)
{
    /* The beta draws one image per frame. Four objects leave room for small
     * extensions, while bounding explicit vertex/index flushes to 560 bytes
     * with the audited SDK, instead of the default 4096-object allocation. */
    beta_graphics_capture_begin();
    beta_graphics_stats_reset();
    bool ok=C2D_Init(4);
    beta_graphics_capture_end(ok);
    return ok;
}
void beta_graphics_frame_end(void)
{
    /* Emit all geometry before flushing its captured backing allocations.
     * C2D's frame-end hook then resets its indices; it does not write vertices.
     * GX_CMDLIST_FLUSH makes GSP flush the final command list, including the
     * frame-end hook's commands, and suppresses Citro3D's whole-heap flush.
     * Unexpected SDK allocations or cache errors preserve the full flush. */
    C2D_Flush();
    size_t bytes=0;
    bool ok=beta_graphics_flush_buffers(&bytes);
    stats.frames++;stats.geometry_flush_bytes+=(uint32_t)bytes;
    if(!ok)stats.full_heap_fallbacks++;
    C3D_FrameEnd(ok?GX_CMDLIST_FLUSH:0);
}
