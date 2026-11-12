/* libctru's linear heap is shared by decoder, conversion, audio and graphics.
 * Serialize every external allocator entry in this beta, including libraries. */
#include <3ds.h>
#include <stddef.h>
#include "graphics_cache.h"
static LightLock allocator_lock=1;
/* Captured only while C2D_Init runs before any streaming workers start.
 * Keep the allocation ranges, never private Citro2D structure offsets. */
static struct {void *memory;size_t bytes;} graphics_buffers[2];
static unsigned graphics_count;
static bool graphics_capture,graphics_valid;
void beta_graphics_capture_begin(void)
{
    LightLock_Lock(&allocator_lock);
    graphics_capture=true;graphics_valid=false;graphics_count=0;
    LightLock_Unlock(&allocator_lock);
}
void beta_graphics_capture_end(bool initialized)
{
    LightLock_Lock(&allocator_lock);
    graphics_capture=false;graphics_valid=initialized&&graphics_count==2;
    LightLock_Unlock(&allocator_lock);
}
bool beta_graphics_flush_buffers(size_t *flushed_bytes)
{
    LightLock_Lock(&allocator_lock);
    bool ok=graphics_valid;
    *flushed_bytes=0;
    for(unsigned i=0;ok&&i<2;i++) {
        ok=R_SUCCEEDED(GSPGPU_FlushDataCache(graphics_buffers[i].memory,graphics_buffers[i].bytes));
        if(ok)*flushed_bytes+=graphics_buffers[i].bytes;
    }
    LightLock_Unlock(&allocator_lock);return ok;
}
void *__real_linearMemAlign(size_t bytes,size_t alignment);
void __real_linearFree(void *memory);
void *__wrap_linearMemAlign(size_t bytes,size_t alignment)
{
    LightLock_Lock(&allocator_lock);
    void *memory=__real_linearMemAlign(bytes,alignment);
    if(graphics_capture&&memory) {
        if(graphics_count<2) {
            graphics_buffers[graphics_count].memory=memory;
            graphics_buffers[graphics_count].bytes=bytes;
        }
        graphics_count++;
    }
    LightLock_Unlock(&allocator_lock);return memory;
}
void *__wrap_linearAlloc(size_t bytes)
{
    /* Do not call real linearAlloc: it may forward to wrapped MemAlign. */
    return __wrap_linearMemAlign(bytes,128);
}
void __wrap_linearFree(void *memory)
{
    LightLock_Lock(&allocator_lock);
    for(unsigned i=0;i<2;i++)if(memory&&graphics_buffers[i].memory==memory)graphics_valid=false;
    __real_linearFree(memory);LightLock_Unlock(&allocator_lock);
}
