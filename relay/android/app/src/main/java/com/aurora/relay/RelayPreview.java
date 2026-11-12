package com.aurora.relay;
import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.View;
/** Foreground-only view of the current picture; no settings or crop diagram. */
final class RelayPreview extends View {
 private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
 private Bitmap bitmap;private RelayEngine engine;private byte[] last;private final RectF imageRect=new RectF();
 RelayPreview(Context c){super(c);setContentDescription("Live PlayStation picture");}
 void update(RelayEngine e){
  if(engine!=e){if(engine!=null)engine.previewEnabled=false;clear();engine=e;}
  if(e!=null){e.previewEnabled=true;boolean current=(e.sourceState==RelayEngine.SOURCE_ACTIVE||e.sourceState==RelayEngine.SOURCE_TEST)&&SystemClock.elapsedRealtime()-e.previewStamp<3000;
   byte[] jpg=current?e.previewJpeg:null;if(jpg==null)clear();else if(jpg!=last){last=jpg;Bitmap next=BitmapFactory.decodeByteArray(jpg,0,jpg.length);if(bitmap!=null)bitmap.recycle();bitmap=next;}
  }else clear();invalidate();
 }
 private void clear(){last=null;if(bitmap!=null)bitmap.recycle();bitmap=null;}
 @Override protected void onMeasure(int w,int h){int width=MeasureSpec.getSize(w);setMeasuredDimension(width,resolveSize(Math.max(1,width*9/16),h));}
 @Override protected void onDraw(Canvas c){super.onDraw(c);float d=getResources().getDisplayMetrics().density;imageRect.set(0,0,getWidth(),getHeight());paint.setColor(0xff0c1729);c.drawRoundRect(imageRect,12*d,12*d,paint);
  if(bitmap==null){paint.setColor(0xffafc0d7);paint.setTextSize(14*d);paint.setTextAlign(Paint.Align.CENTER);c.drawText("Waiting for live picture",getWidth()/2f,getHeight()/2f,paint);paint.setTextAlign(Paint.Align.LEFT);return;}
  float scale=Math.min((float)getWidth()/bitmap.getWidth(),(float)getHeight()/bitmap.getHeight()),w=bitmap.getWidth()*scale,h=bitmap.getHeight()*scale;imageRect.set((getWidth()-w)/2,(getHeight()-h)/2,(getWidth()+w)/2,(getHeight()+h)/2);c.drawBitmap(bitmap,null,imageRect,paint);
 }
}
