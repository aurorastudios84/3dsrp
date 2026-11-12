package com.aurora.relay;

/** PS5 input settings; relay picture size and output pacing are independent. */
final class StreamProfile {
 static final int[] HEIGHTS={360,540,720,1080};
 static final int DEFAULT_HEIGHT=360,DEFAULT_BITRATE=3000,DEFAULT_FPS=30;
 final int width,height,bitrate,fps;
 StreamProfile(int height,int bitrate){this(height,bitrate,DEFAULT_FPS);}
 StreamProfile(int height,int bitrate,int fps){
  switch(height){case 360:width=640;break;case 540:width=960;break;case 720:width=1280;break;case 1080:width=1920;break;default:throw new IllegalArgumentException("Choose 360p, 540p, 720p or 1080p");}
  if(bitrate<100||bitrate>50000)throw new IllegalArgumentException("PS5 bitrate must be 100–50000 kbps");
  if(fps!=30&&fps!=60)throw new IllegalArgumentException("Choose 30 or 60 console frames per second");
  this.height=height;this.bitrate=bitrate;this.fps=fps;
 }
 static int parseBitrate(String text){try{return Integer.parseInt(text.trim());}catch(NumberFormatException e){throw new IllegalArgumentException("Enter a whole-number PS5 bitrate in kbps");}}
 String json(){return "["+width+","+height+","+fps+","+bitrate+"]";}
 String label(){return height+"p / "+fps+" fps / "+bitrate+" kbps";}
}
