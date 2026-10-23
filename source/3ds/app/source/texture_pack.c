#include "texture_pack.h"

bool rgb565_texture_pack(uint16_t *texture,size_t capacity,unsigned tw,unsigned th,
                     const uint16_t *source,unsigned width,unsigned height,unsigned stride)
{
    if(!texture || !source || tw<8 || th<8 || tw>1024 || th>1024 ||
       (tw&(tw-1)) || (th&(th-1)) || !width || !height || width>=tw || height>=th ||
       stride<width || stride>1024 || capacity<(size_t)tw*th) return false;
    /* Table is the low three coordinate bits interleaved with zeroes. */
    static const unsigned spread[8]={0,1,4,5,16,17,20,21};
    unsigned ew=(width+8u)&~7u,eh=(height+8u)&~7u;
    for(unsigned ty=0;ty<eh;ty+=8) for(unsigned tx=0;tx<ew;tx+=8) {
        uint16_t *tile=texture+(size_t)ty*tw+tx*8u;
        for(unsigned y=0;y<8;y++) {
            unsigned sy=ty+y<height ? ty+y : height-1;
            const uint16_t *row=source+(size_t)sy*stride;
            for(unsigned x=0;x<8;x++) {
                unsigned sx=tx+x<width ? tx+x : width-1;
                tile[spread[x]+2u*spread[y]]=row[sx];
            }
        }
    }
    return true;
}
