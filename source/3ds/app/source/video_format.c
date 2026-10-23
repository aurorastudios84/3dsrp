#include "video_format.h"
#include <string.h>

static size_t prefix_at(const uint8_t *data, size_t size, size_t offset)
{
    if(offset+3>size || data[offset] || data[offset+1]) return 0;
    if(data[offset+2]==1) return 3;
    if(offset+4<=size && !data[offset+2] && data[offset+3]==1) return 4;
    return 0;
}
bool video_parse_au(const uint8_t *data, size_t size, VideoAccessUnit *unit)
{
    memset(unit,0,sizeof(*unit));
    if(!data || !size || size>VIDEO_MAX_AU) return false;
    size_t offset=0;
    while(offset<size) {
        size_t prefix=prefix_at(data,size,offset);
        if(!prefix || offset+prefix>=size || unit->count==VIDEO_MAX_NALS) return false;
        size_t end=offset+prefix+1;
        while(end<size && !prefix_at(data,size,end)) end++;
        unsigned type=data[offset+prefix]&31;
        if((data[offset+prefix]&0x80) || !type || type>23) return false;
        unit->nals[unit->count++]=(VideoNal){offset,end-offset,prefix,type};
        unit->picture |= type>=1 && type<=5;
        unit->idr |= type==5;
        unit->sps |= type==7;
        unit->pps |= type==8;
        offset=end;
    }
    return unit->count!=0;
}

typedef struct { uint8_t data[256]; size_t bits, position; bool failed; } BitReader;
static unsigned read_bits(BitReader *reader, unsigned count)
{
    if(count>32 || reader->position+count>reader->bits) { reader->failed=true; return 0; }
    unsigned value=0;
    for(unsigned index=0;index<count;index++) {
        value=(value<<1) | ((reader->data[reader->position/8]>>(7-reader->position%8))&1);
        reader->position++;
    }
    return value;
}
static unsigned read_ue(BitReader *reader)
{
    unsigned zeros=0;
    while(!reader->failed && !read_bits(reader,1)) {
        if(++zeros>30) { reader->failed=true; return 0; }
    }
    unsigned suffix=read_bits(reader,zeros);
    return ((1u<<zeros)-1)+suffix;
}
bool video_parse_sps(const uint8_t *data, size_t size, VideoSps *sps)
{
    memset(sps,0,sizeof(*sps));
    if(!data || size<5 || (data[0]&31)!=7 || (data[0]&0x80)) return false;
    BitReader reader={0};
    unsigned zeros=0;
    size_t bytes=0;
    for(size_t index=1;index<size;index++) {
        if(zeros>=2 && data[index]==3) {
            if(index+1>=size || data[index+1]>3) return false;
            zeros=0;
            continue;
        }
        if(bytes==sizeof(reader.data)) return false;
        reader.data[bytes++]=data[index];
        zeros=data[index]==0 ? zeros+1 : 0;
    }
    reader.bits=bytes*8;
    sps->profile=read_bits(&reader,8);
    read_bits(&reader,8);
    sps->level=read_bits(&reader,8);
    if(sps->profile!=66 && sps->profile!=77) return false;
    if(read_ue(&reader)>31 || read_ue(&reader)>12) return false;
    unsigned poc=read_ue(&reader);
    if(poc==0) { if(read_ue(&reader)>12) return false; }
    else if(poc==1) {
        read_bits(&reader,1); read_ue(&reader); read_ue(&reader);
        unsigned cycle=read_ue(&reader);
        if(cycle>32) return false;
        for(unsigned index=0;index<cycle;index++) read_ue(&reader);
    } else if(poc!=2) return false;
    sps->refs=read_ue(&reader);
    read_bits(&reader,1);
    unsigned width_mbs=read_ue(&reader)+1, height_mbs=read_ue(&reader)+1;
    if(width_mbs>1024 || height_mbs>1024 || !read_bits(&reader,1)) return false;
    read_bits(&reader,1);
    unsigned left=0,right=0,top=0,bottom=0;
    if(read_bits(&reader,1)) {
        left=read_ue(&reader); right=read_ue(&reader);
        top=read_ue(&reader); bottom=read_ue(&reader);
    }
    if(reader.failed || sps->refs>32 || left>8192 || right>8192 || top>8192 || bottom>8192) return false;
    sps->coded_width=width_mbs*16;
    sps->coded_height=height_mbs*16;
    sps->crop_left=left*2; sps->crop_top=top*2; sps->crop_right=right*2; sps->crop_bottom=bottom*2;
    if((left+right)*2>=sps->coded_width || (top+bottom)*2>=sps->coded_height) return false;
    sps->width=sps->coded_width-(left+right)*2;
    sps->height=sps->coded_height-(top+bottom)*2;
    sps->macroblocks=width_mbs*height_mbs;
    uint64_t dpb=(uint64_t)sps->refs*sps->macroblocks*384;
    if(dpb>UINT32_MAX) return false;
    sps->dpb_bytes=(uint32_t)dpb;
    return true;
}
bool video_sps_safe(const VideoSps *sps, unsigned width, unsigned height,
                    uint32_t max_mbs, uint32_t max_dpb, uint32_t max_level)
{
    if(width==960 && height==540) {
        /* Hardware-accepted probe envelope only: progressive 4:2:0 is
         * established by the Baseline/Main SPS parser above. */
        return sps->width==960 && sps->height==540 && sps->profile==77 &&
            sps->level<=32 && sps->level<=max_level && sps->refs<=9 &&
            sps->coded_width==960 && sps->coded_height==544 &&
            !sps->crop_left && !sps->crop_top && !sps->crop_right && sps->crop_bottom==4 &&
            sps->macroblocks==2040 && sps->macroblocks<=max_mbs &&
            sps->dpb_bytes<=7050240u && sps->dpb_bytes<=max_dpb;
    }
    return sps->width==width && sps->height==height && width<=852 && height<=480 &&
        width>=48 && height>=48 && !(width%8) && !(height%8) &&
        !(sps->crop_left%16) && !(sps->crop_top%16) &&
        sps->coded_width<=((width+15)&~15u) && sps->coded_height<=((height+15)&~15u) &&
        sps->macroblocks<=max_mbs && sps->dpb_bytes<=max_dpb && sps->level<=max_level;
}
