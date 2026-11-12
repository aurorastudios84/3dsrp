package com.aurora.relay;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;

/** Runs on an emulator only. Uses actual Android Views without UIAutomator or external test libraries. */
public final class UiReview extends Instrumentation {
 boolean pinOnly;MainActivity activity;SharedPreferences legacy,consoles;Map<String,?> oldLegacy,oldConsoles;Bundle result=new Bundle();
 interface Check {boolean value();}
 void require(boolean ok,String message){if(!ok)throw new AssertionError(message);}
 void main(Runnable r){final Throwable[] error={null};runOnMainSync(()->{try{r.run();}catch(Throwable e){error[0]=e;}});if(error[0]!=null)throw new AssertionError(error[0]);}
 void waitFor(Check condition,String message)throws Exception{long deadline=SystemClock.elapsedRealtime()+90000;while(SystemClock.elapsedRealtime()<deadline){final boolean[] ok={false};main(()->ok[0]=condition.value());if(ok[0])return;Thread.sleep(200);}throw new AssertionError(message);}
 Button button(View root,String label){if(root instanceof Button&&label.contentEquals(((Button)root).getText())&&root.isShown())return (Button)root;if(root instanceof ViewGroup){ViewGroup g=(ViewGroup)root;for(int n=0;n<g.getChildCount();n++){Button b=button(g.getChildAt(n),label);if(b!=null)return b;}}return null;}
 boolean text(View root,String value){if(root instanceof TextView&&((TextView)root).getText().toString().contains(value)&&root.isShown())return true;if(root instanceof ViewGroup){ViewGroup g=(ViewGroup)root;for(int n=0;n<g.getChildCount();n++)if(text(g.getChildAt(n),value))return true;}return false;}
 void click(String label){Button b=button(activity.getWindow().getDecorView(),label);require(b!=null,"Missing button: "+label);b.performClick();}
 void capture(String name){
  waitForIdleSync();main(()->{View view=activity.getWindow().getDecorView();require(view.getWidth()>0&&view.getHeight()>0,"View not laid out");Bitmap bitmap=Bitmap.createBitmap(view.getWidth(),view.getHeight(),Bitmap.Config.ARGB_8888);view.draw(new Canvas(bitmap));try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getFilesDir(),"home34-"+name+".png"))){bitmap.compress(Bitmap.CompressFormat.PNG,100,out);}catch(IOException e){throw new RuntimeException(e);}finally{bitmap.recycle();}});
 }
 void launch(boolean fixture){Intent i=new Intent(getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);if(fixture)i.putExtra("selftest",true).putExtra("source_fps",30);activity=(MainActivity)startActivitySync(i);}
 void restore(SharedPreferences prefs,Map<String,?> values){SharedPreferences.Editor e=prefs.edit().clear();for(Map.Entry<String,?> v:values.entrySet()){Object x=v.getValue();String k=v.getKey();if(x instanceof String)e.putString(k,(String)x);else if(x instanceof Integer)e.putInt(k,(Integer)x);else if(x instanceof Long)e.putLong(k,(Long)x);else if(x instanceof Boolean)e.putBoolean(k,(Boolean)x);else if(x instanceof Float)e.putFloat(k,(Float)x);else if(x instanceof Set)e.putStringSet(k,(Set<String>)x);}e.commit();}
 @Override public void onCreate(Bundle args){super.onCreate(args);pinOnly=args!=null&&"true".equals(args.getString("pin_state_only"));start();}
 @Override public void onStart(){
  int code=Activity.RESULT_CANCELED;
  try{
   require(Build.PRODUCT.contains("sdk")||Build.FINGERPRINT.contains("generic"),"Emulator only: refusing to change physical device data");
   legacy=getTargetContext().getSharedPreferences("MainActivity",0);consoles=getTargetContext().getSharedPreferences("consoles",0);oldLegacy=legacy.getAll();oldConsoles=consoles.getAll();legacy.edit().clear().commit();consoles.edit().clear().commit();
   // Prevent the legacy pair migration from seeing old inputs; pairing files themselves are never edited.
   if(pinOnly){
    launch(false);main(()->{
     activity.showSetup(null,4,null);EditText pin=activity.wizard.pinField;pin.setText("12345678");Bundle saved=new Bundle();activity.onSaveInstanceState(saved);
     require(!pin.isSaveEnabled()&&pin.getImportantForAutofill()==View.IMPORTANT_FOR_AUTOFILL_NO,"PIN view persistence enabled");
     Bundle hierarchy=saved.getBundle("android:viewHierarchyState");android.util.SparseArray<android.os.Parcelable> views=hierarchy==null?null:hierarchy.getSparseParcelableArray("android:views");
     require(views==null||views.get(pin.getId())==null,"PIN saved in Android view state");require(!legacy.getAll().toString().contains("12345678")&&!consoles.getAll().toString().contains("12345678"),"PIN persisted to preferences");
    });result.putString("stream","PASS transient PIN excluded from Android saved view state and preferences");code=Activity.RESULT_OK;return;
   }
   launch(false);waitFor(()->activity.wizard!=null&&activity.wizard.getWidth()>0,"First-run tutorial not visible");capture("tutorial");
   main(()->{
    require(activity.screen==MainActivity.SETUP&&activity.wizard.step==0,"Tutorial must open on first launch");
    activity.wizard.next();require(activity.wizard.step==1&&text(activity.wizard,"System → Remote Play"),"PS5 enable instructions");
    activity.wizard.next();require(activity.wizard.step==2,"IP step");activity.wizard.ipField.setText("");activity.wizard.next();require(activity.wizard.step==2&&activity.wizard.error.isShown(),"Reject empty IP");activity.wizard.ipField.setText("192.0.2.10");activity.wizard.next();
    activity.wizard.accountField.setText("-");activity.wizard.next();require(activity.wizard.step==3,"Reject invalid Account ID");activity.wizard.accountField.setText("1");activity.wizard.next();require(text(activity.wizard,"Link Device"),"PS5 PIN instructions");
    activity.wizard.pinField.setText("123");activity.wizard.next();require(activity.wizard.step==4,"Reject short PIN");activity.wizard.pinField.setText("12345678");activity.wizard.next();require(activity.wizard.step==5&&activity.store.consoles.size()==1,"Save console");
    require(!legacy.getAll().toString().contains("12345678")&&!consoles.getAll().toString().contains("12345678"),"PIN must not be persisted");click("Go to home");
    require(activity.screen==MainActivity.HOME&&activity.consoles.isShown()&&activity.quality.isShown()&&activity.start.isShown(),"Simple home controls");require(!activity.resolution.isShown()&&!activity.diagnostics.isShown()&&!activity.preview.isShown(),"Settings cannot appear on home");
   });waitFor(()->!activity.primaryAddress().isEmpty(),"Phone address unavailable");capture("home");
   main(()->{
    activity.quality.check(activity.sizeIds[2]);require(legacy.getInt("image_size",-1)==2,"Quality saved");activity.showPage(MainActivity.SETTINGS);require(activity.phone.isShown()&&activity.resolution.isShown(),"Address stays visible in settings");activity.framerate.setSelection(1);activity.bitrate.setText("4000");activity.saveSettings();require(legacy.getInt("source_fps",0)==60&&legacy.getInt("source_bitrate",0)==4000,"Settings saved");activity.defaults.performClick();require(legacy.getInt("source_fps",0)==30&&legacy.getInt("source_height_magnify",0)==360,"Restore defaults");
   });capture("settings");main(()->activity.showPage(MainActivity.DIAGNOSTICS));capture("diagnostics");
   main(()->{
    activity.showSetup(null,0,null);activity.wizard.type.setSelection(1);activity.wizard.next();require(text(activity.wizard,"Remote Play Connection Settings"),"PS4 enable instructions");activity.wizard.next();require(text(activity.wizard,"Network → View Connection Status"),"PS4 IP instructions");
    activity.showSetup(null,4,null);activity.wizard.ps5=false;activity.wizard.render();require(text(activity.wizard,"Add Device"),"PS4 PIN instructions");
    activity.finishGuide();activity.finish();
   });launch(false);waitFor(()->activity.screen==MainActivity.HOME,"Tutorial repeated after setup");
   main(()->{require(activity.selected()!=null&&activity.selected().local.equals("192.0.2.10"),"Saved console preserved");activity.finish();});
   // No UDP client is present: the phone must still show its live codec fixture picture.
   launch(true);waitFor(()->{RelayEngine e=RelayService.session();return e!=null&&e.previewJpeg!=null&&activity.screen==MainActivity.LIVE;},"No live preview before 3DS connection");
   main(()->{RelayEngine e=RelayService.session();require(!e.hasPeer(),"Test unexpectedly has a 3DS peer");require(activity.phone.isShown()&&activity.preview.isShown(),"Live picture and phone address");require(!activity.quality.isShown(),"Live view must hide settings");});capture("live");
   main(()->{activity.showPage(MainActivity.SETTINGS);activity.updateSession();require(!activity.apply.isEnabled(),"Settings editable during session");activity.disconnect(false);});waitFor(()->!RelayService.active(),"Service did not stop");
   result.putString("stream","PASS first launch, PS5/PS4 steps, field validation, transient PIN, console persistence, home/settings/diagnostics, defaults, live preview with no 3DS, Stop");code=Activity.RESULT_OK;
  }catch(Throwable error){result.putString("error",android.util.Log.getStackTraceString(error));}
  finally{
   try{getTargetContext().startService(new Intent(getTargetContext(),RelayService.class).setAction("STOP"));waitFor(()->!RelayService.active(),"Cleanup");}catch(Exception ignored){}
   if(activity!=null)main(()->activity.finish());if(oldLegacy!=null)restore(legacy,oldLegacy);if(oldConsoles!=null)restore(consoles,oldConsoles);finish(code,result);
  }
 }
}
