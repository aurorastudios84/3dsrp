package com.aurora.relay;
import java.nio.ByteBuffer;

/** Stateful low-pass filtering before integer downsampling to 12 kHz mono. */
final class PcmResampler {
 private final int step,channels;
 private final double[] taps=new double[63],history=new double[63];
 private int cursor,phase;
 PcmResampler(int rate,int channels){
  if(rate<12000||rate%12000!=0||channels<1||channels>8)throw new IllegalArgumentException("Unsupported PCM format: "+rate+" Hz / "+channels+" channels");
  step=rate/12000;this.channels=channels;
  double cutoff=0.45/step,sum=0;
  for(int i=0;i<taps.length;i++){int n=i-taps.length/2;double sinc=n==0?2*cutoff:Math.sin(2*Math.PI*cutoff*n)/(Math.PI*n);taps[i]=sinc*(0.5-0.5*Math.cos(2*Math.PI*i/(taps.length-1)));sum+=taps[i];}
  for(int i=0;i<taps.length;i++)taps[i]/=sum;
 }
 byte[] convert(ByteBuffer pcm){
  int frames=pcm.remaining()/(channels*2),outputs=(phase+frames)/step;byte[] out=new byte[outputs*2];int at=0;
  for(int i=0;i<frames;i++){
   int sum=0;for(int ch=0;ch<channels;ch++)sum+=pcm.getShort();
   history[cursor]=(double)sum/channels;cursor=(cursor+1)%history.length;
   if(++phase==step){phase=0;double value=0;int pos=cursor;
    for(int k=0;k<taps.length;k++){if(--pos<0)pos=history.length-1;value+=history[pos]*taps[k];}
    int sample=(int)Math.round(value);sample=Math.max(-32768,Math.min(32767,sample));out[at++]=(byte)sample;out[at++]=(byte)(sample>>8);
   }
  }
  return out;
 }
}
