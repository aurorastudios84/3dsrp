package com.aurora.relay;
import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.os.*;
import android.net.wifi.WifiManager;

/** Owns the single relay session independently of the Activity and display. */
public final class RelayService extends Service {
 static volatile RelayService current;
 static volatile boolean cleaning;
 static volatile String lastMessage="Ready";
 volatile RelayEngine engine;volatile boolean stopping;
 volatile boolean connected,awaitingCloseChoice;boolean consoleSession;String label;int closeSequence;
 private PowerManager.WakeLock cpu;private WifiManager.WifiLock wifi;
 private final Handler main=new Handler(Looper.getMainLooper());
 private String consoleId;
 static boolean active(){return current!=null||cleaning;}
 static RelayEngine session(){RelayService s=current;return s==null?null:s.engine;}
 @Override public void onCreate(){super.onCreate();current=this;IdleUsageRelay.stop();}
 @Override public IBinder onBind(Intent i){return null;}
 @Override public int onStartCommand(Intent intent,int flags,int startId){
  if(intent==null){stopSelf();return START_NOT_STICKY;}
  if("STOP".equals(intent.getAction())){lastMessage="Stopping relay…";stopRelay();return START_NOT_STICKY;}
  if("REST".equals(intent.getAction())){stopRelay(true);return START_NOT_STICKY;}
  if(cleaning){stopSelf();return START_NOT_STICKY;}
  if(engine!=null||stopping)return START_NOT_STICKY;
  try{
   label=intent.getStringExtra("label");consoleId=intent.getStringExtra("consoleId");
   showNotification(false);
   acquireLocks();
   boolean fixture=intent.getBooleanExtra("fixture",false),test=intent.getBooleanExtra("test",false);consoleSession=!fixture&&!test;
   engine=new RelayEngine(test,70,new StreamProfile(intent.getIntExtra("height",StreamProfile.DEFAULT_HEIGHT),intent.getIntExtra("bitrate",StreamProfile.DEFAULT_BITRATE),intent.getIntExtra("fps",StreamProfile.DEFAULT_FPS)),new ImageSize(intent.getIntExtra("size",1)),value->{
    if(current!=this||stopping)return;
    lastMessage=value;
    if(value.startsWith("Console connected")){connected=true;main.post(()->{if(!stopping)showNotification(false);});}
    if(value.startsWith("Disconnected:"))connected=false;
    if(value.equals("3DS stream closed"))main.post(this::offerCloseChoice);
    if(value.equals("3DS connected"))main.post(()->{if(awaitingCloseChoice){awaitingCloseChoice=false;closeSequence++;lastMessage="3DS reconnected";showNotification(false);}});
    if("Console paired".equals(value)&&consoleId!=null)getSharedPreferences("consoles",MODE_PRIVATE).edit().putBoolean("paired-"+consoleId,true).apply();
    if(value.startsWith("Video decode failed")||value.startsWith("Sound decode failed")||value.startsWith("Disconnected:"))main.post(this::stopRelay);
   });
   lastMessage="Connecting to "+label+"…";
   if(fixture)engine.startFixtures(getAssets());else engine.start(intent.getStringExtra("host"),intent.getByteArrayExtra("account"),intent.getStringExtra("pin"),intent.getStringExtra("pairFile"),intent.getBooleanExtra("ps5",true));
   main.post(checkFailure);
  }catch(Exception ex){lastMessage=ex.getMessage();stopRelay();}
  return START_NOT_STICKY;
 }
 private final Runnable checkFailure=new Runnable(){public void run(){RelayEngine e=engine;if(e!=null&&e.failure()!=null){lastMessage=e.failure();stopRelay();}else if(!stopping)main.postDelayed(this,500);}};
 @android.annotation.SuppressLint("WakelockTimeout")
 private void acquireLocks(){
  cpu=((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"3ds-relay:stream");cpu.setReferenceCounted(false);cpu.acquire();
  WifiManager manager=(WifiManager)getApplicationContext().getSystemService(WIFI_SERVICE);
  if(manager!=null){wifi=manager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF,"3ds-relay:wifi");wifi.setReferenceCounted(false);wifi.acquire();}
 }
 private void releaseLocks(){if(wifi!=null&&wifi.isHeld())wifi.release();if(cpu!=null&&cpu.isHeld())cpu.release();}
 private void showNotification(boolean choice){
  NotificationManager manager=getSystemService(NotificationManager.class);manager.createNotificationChannel(new NotificationChannel("relay","Active relay",NotificationManager.IMPORTANCE_LOW));
  PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
  PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,RelayService.class).setAction("STOP"),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
  PendingIntent rest=PendingIntent.getService(this,2,new Intent(this,RelayService.class).setAction("REST"),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
  Notification.Builder b=new Notification.Builder(this,"relay").setSmallIcon(com.aurora.relay.R.drawable.ic_portal).setContentTitle("3DSRP Companion · "+label).setContentText(choice?"Stream closed: leave console on or enter Rest Mode?":"Streaming continues with the screen off").setContentIntent(open).setOngoing(true).addAction(new Notification.Action.Builder(null,choice?"Leave on":"Disconnect",stop).build());
  if(consoleSession&&connected)b.addAction(new Notification.Action.Builder(null,"Rest Mode",rest).build());
  Notification n=b.build();if(Build.VERSION.SDK_INT>=29)startForeground(1,n,ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);else startForeground(1,n);
 }
 private void offerCloseChoice(){
  if(stopping)return;
  if(!consoleSession||!connected){stopRelay();return;}
  if(awaitingCloseChoice)return;awaitingCloseChoice=true;final int request=++closeSequence;lastMessage="3DS stream closed. Choose Leave on or Rest Mode.";showNotification(true);
  // An unanswered offer only disconnects; it never puts the console to sleep.
  main.postDelayed(()->{if(awaitingCloseChoice&&request==closeSequence){lastMessage="Stopping relay…";stopRelay();}},60000);
 }
 private void stopRelay(){stopRelay(false);}
 private void stopRelay(boolean rest){
  if(stopping)return;stopping=true;awaitingCloseChoice=false;main.removeCallbacksAndMessages(null);final RelayEngine e=engine;
  new Thread(()->{try{if(e!=null){if(rest)lastMessage=e.nativeRest()?"Rest Mode requested; relay disconnected":"Could not request Rest Mode; relay disconnected";e.stop();}}finally{main.post(()->{engine=null;if(lastMessage.startsWith("Stopping relay"))lastMessage="Relay stopped";releaseLocks();stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();});}},"relay-stop").start();
 }
 @Override public void onTaskRemoved(Intent intent){
  if(!stopping){lastMessage="Stopping relay…";stopRelay();}
  super.onTaskRemoved(intent);
 }
 @Override public void onDestroy(){main.removeCallbacksAndMessages(null);RelayEngine e=engine;engine=null;if(!stopping&&e!=null){cleaning=true;new Thread(()->{try{e.stop();}finally{cleaning=false;}},"relay-destroy").start();}releaseLocks();if(current==this)current=null;super.onDestroy();}
}
