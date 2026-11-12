#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
from PIL import Image
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp)
 (p/'test.c').write_text(r'''
#include "jpeg_encode.h"
#include <assert.h>
#include <string.h>
int main(int argc,char **argv){
 unsigned width=atoi(argv[2]),height=atoi(argv[3]),nvbytes=relay_nv21_bytes(width,height);
 unsigned char *nv=malloc(nvbytes);assert(nv);memset(nv,81,width*height);
 for(unsigned i=width*height;i<nvbytes;i+=2){nv[i]=240;nv[i+1]=90;}
 /* Last luma/chroma rows must survive the odd-height encoder. */
 memset(nv+width*(height-1),235,width);memset(nv+width*height+width*((height-1)/2),128,width);
 for(int q=1;q<=100;q+=33){unsigned long size=0;unsigned char *jpeg=relay_encode_jpeg(nv,q,&size,width,height);assert(jpeg&&size>100);free(jpeg);}
 unsigned long size=0;unsigned char *jpeg=relay_encode_jpeg(nv,95,&size,width,height);assert(jpeg);FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(jpeg,1,size,f)==size);fclose(f);free(jpeg);free(nv);
}
''')
 subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-I'+str(root/'common'),'-I'+str(root/'output/host-jpeg'),'-I'+str(root/'vendor/libjpeg-turbo-3.1.3/src'),str(p/'test.c'),str(root/'output/host-jpeg/libjpeg.a'),'-lm','-o',str(p/'test')],check=True)
 for width,height in [(240,135),(320,180),(400,225)]:
  subprocess.run([p/'test',p/'out.jpg',str(width),str(height)],check=True)
  image=Image.open(p/'out.jpg').convert('RGB');assert image.size==(width,height)
  red=image.getpixel((100,100));assert red[0]>200 and red[1]<30 and red[2]<30,red
  edge=image.getpixel((100,height-1));assert min(edge)>180,edge
print('PASS native JPEG encoder: all three image sizes, YUV colour order, last odd row and quality bounds under ASan/UBSan')
