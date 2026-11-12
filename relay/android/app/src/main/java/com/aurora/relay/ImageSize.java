package com.aurora.relay;
final class ImageSize {
 static final String[] LABELS={"Low · 240 × 135","Balanced · 320 × 180 · Default","High · 400 × 225"};
 final int width,height,index;
 ImageSize(int index){if(index<0||index>2)throw new IllegalArgumentException("Invalid image size");this.index=index;width=new int[]{240,320,400}[index];height=new int[]{135,180,225}[index];}
 int bytes(){return width*height+width*((height+1)/2);}
}
