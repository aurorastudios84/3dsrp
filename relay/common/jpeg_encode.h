#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <jpeglib.h>
#include "scale.h"
typedef struct { struct jpeg_compress_struct jpeg; struct jpeg_error_mgr error; jmp_buf jump; unsigned char *bytes; unsigned long size; } RelayJpeg;
static void relay_jpeg_error(j_common_ptr c){RelayJpeg *state=(RelayJpeg*)c;longjmp(state->jump,1);}
/* Caller owns the returned allocation. Keep the odd-height chroma edge intact. */
static unsigned char *relay_encode_jpeg(const unsigned char *nv,int quality,unsigned long *size,unsigned width,unsigned height){
 if(!nv||!size||!relay_image_size(width,height)||quality<1||quality>100)return NULL;
 RelayJpeg *s=(RelayJpeg*)calloc(1,sizeof(*s));if(!s)return NULL;
 s->jpeg.err=jpeg_std_error(&s->error);s->error.error_exit=relay_jpeg_error;
 if(setjmp(s->jump)){jpeg_destroy_compress(&s->jpeg);free(s->bytes);free(s);return NULL;}
 jpeg_create_compress(&s->jpeg);jpeg_mem_dest(&s->jpeg,&s->bytes,&s->size);
 s->jpeg.image_width=width;s->jpeg.image_height=height;s->jpeg.input_components=3;s->jpeg.in_color_space=JCS_YCbCr;
 jpeg_set_defaults(&s->jpeg);jpeg_set_quality(&s->jpeg,quality,TRUE);s->jpeg.dct_method=JDCT_FASTEST;
 jpeg_start_compress(&s->jpeg,TRUE);unsigned char row[RELAY_WIDTH*3];
 while(s->jpeg.next_scanline<height){
  unsigned y=s->jpeg.next_scanline;
  for(unsigned x=0;x<width;x++){
   unsigned uv=width*height+(y/2)*width+(x&~1u);
   row[3*x]=nv[y*width+x];row[3*x+1]=nv[uv+1];row[3*x+2]=nv[uv];
  }
  JSAMPROW rows[1]={row};jpeg_write_scanlines(&s->jpeg,rows,1);
 }
 jpeg_finish_compress(&s->jpeg);jpeg_destroy_compress(&s->jpeg);unsigned char *out=s->bytes;*size=s->size;free(s);return out;
}
