#pragma once
static inline int relay_image_size(unsigned w,unsigned h){return (w==400&&h==225)||(w==320&&h==180)||(w==240&&h==135);}
static inline unsigned relay_nv21_bytes(unsigned w,unsigned h){return w*h+w*((h+1)/2);}
