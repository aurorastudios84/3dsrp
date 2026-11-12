package com.aurora.relay;

import android.content.Context;
import android.graphics.Typeface;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.LinkedHashMap;
import java.util.Locale;

/** Readable session details; versions are verified by the existing HELLO handshake. */
final class DiagnosticsPanel extends LinearLayout {
 private final LinkedHashMap<String,TextView> values=new LinkedHashMap<>();
 private final TextView compatibility;
 private static final int MUTED=0xffafc0d7, TEXT=0xffeff5ff;
 DiagnosticsPanel(Context context){
  super(context);setOrientation(VERTICAL);
  TextView title=label("Diagnostics",21,TEXT);title.setTypeface(null,Typeface.BOLD);addView(title);
  TextView hint=label("Live details for the current relay session.",14,MUTED);addView(hint);
  LinearLayout apps=section("Apps & compatibility");
  row(apps,"Android app");row(apps,"3DS app");row(apps,"Required release");
  compatibility=label("Connect your 3DS to check versions",14,MUTED);apps.addView(compatibility);
  LinearLayout input=section("Console input");row(input,"Source");row(input,"Resolution");row(input,"Frame rate");row(input,"Bitrate");
  LinearLayout picture=section("Picture delivery");row(picture,"3DS address");row(picture,"Image size");row(picture,"Delivery target");row(picture,"Received / decoded / sent");row(picture,"Displayed frames");
  LinearLayout health=section("Health & processing");row(health,"Phone processing");row(health,"3DS decode errors");row(health,"3DS rejected frames");row(health,"Video / audio drops");row(health,"Audio packets sent");row(health,"Data sent");row(health,"Pixel conversion");row(health,"Video decoder");
  LinearLayout usage=section("Usage statistics");row(usage,"Relay listener");row(usage,"Usage delivery");row(usage,"Reports received");row(usage,"Reports rejected");row(usage,"Reports confirmed");row(usage,"Last HTTP response");row(usage,"Last transport error");
  update(null);
 }
 private int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
 private TextView label(String value,int size,int color){TextView v=new TextView(getContext());v.setText(value);v.setTextSize(size);v.setTextColor(color);v.setPadding(0,dp(5),0,dp(5));return v;}
 private LinearLayout section(String title){
  LinearLayout box=new LinearLayout(getContext());box.setOrientation(VERTICAL);box.setPadding(dp(12),dp(10),dp(12),dp(10));
  android.graphics.drawable.GradientDrawable background=new android.graphics.drawable.GradientDrawable();background.setColor(0xff142640);background.setCornerRadius(dp(10));box.setBackground(background);
  LayoutParams params=new LayoutParams(-1,-2);params.topMargin=dp(12);addView(box,params);
  TextView heading=label(title,16,TEXT);heading.setTypeface(null,Typeface.BOLD);box.addView(heading);return box;
 }
 private void row(LinearLayout box,String name){
  View divider=new View(getContext());divider.setBackgroundColor(0xff263b57);box.addView(divider,new LayoutParams(-1,dp(1)));
  LinearLayout line=new LinearLayout(getContext());line.setGravity(android.view.Gravity.TOP);box.addView(line,new LayoutParams(-1,-2));
  TextView key=label(name,14,MUTED),value=label("—",14,TEXT);key.setPadding(0,dp(9),dp(10),dp(9));value.setPadding(0,dp(9),0,dp(9));value.setTextIsSelectable(true);
  line.addView(key,new LayoutParams(0,-2,1));line.addView(value,new LayoutParams(0,-2,1));values.put(name,value);
 }
 private void put(String key,String value){TextView v=values.get(key);if(!value.contentEquals(v.getText()))v.setText(value);}
 void update(RelayEngine e){
  IdleUsageRelay idle=IdleUsageRelay.instance();boolean idleConnected=idle!=null&&idle.hasPeer();
  boolean connected=e!=null?e.hasPeer():idleConnected,mismatch=e!=null&&!e.compatibilityError.isEmpty();
  put("Android app","Release "+BuildConfig.VERSION_CODE);
  put("3DS app",e==null?(idleConnected?"Release "+idle.clientRelease:"Not connected"):(!connected&&!mismatch)?"Not connected":e.clientRelease>0?"Release "+e.clientRelease:"Unrecognized version");
  put("Required release","Both apps: "+Protocol.RELEASE);
  String match=mismatch?"Version mismatch · update both apps":connected?"Versions match · ready":"Connect your 3DS to check versions";
  compatibility.setText(match);compatibility.setTextColor(mismatch?0xffffaaaa:connected?0xff83e0ad:MUTED);
  put("Source",e==null?(idleConnected?"Saved report sync":"No active session"):e.pattern?"Connection test":"PlayStation stream");
  put("Resolution",e==null||e.pattern?"—":e.profile.width+" × "+e.profile.height);
  put("Frame rate",e==null||e.pattern?"—":e.profile.fps+" fps");put("Bitrate",e==null||e.pattern?"—":e.profile.bitrate+" kbps");
  java.net.InetSocketAddress peer=e==null?(idle==null?null:idle.peer):e.peer;
  put("3DS address",connected&&peer!=null?peer.getAddress().getHostAddress():"Not connected");
  put("Image size",e==null?"—":e.width+" × "+e.height);
  if(e!=null)e.updateRates();
  put("Delivery target",e==null?"—":String.format(Locale.US,"%.1f fps",e.videoRate.fps(android.os.SystemClock.elapsedRealtime())));
  put("Received / decoded / sent",e==null?"—":String.format(Locale.US,"%.1f / %.1f / %.1f fps",e.inputFps,e.decodedFps,e.sentFps));
  put("Displayed frames",e!=null&&connected?Long.toString(e.clientShown):"—");
  put("Phone processing",e==null?"—":e.sendAgeUs.get()/1000+" ms");
  put("3DS decode errors",e!=null&&connected?Long.toString(e.clientDecodeErrors):"—");put("3DS rejected frames",e!=null&&connected?Long.toString(e.clientBadFrames):"—");
  put("Video / audio drops",e==null?"—":e.videoDrops.get()+" / "+e.audioDrops.get());
  put("Audio packets sent",e==null?"—":Long.toString(e.audioPackets.get()));put("Data sent",e==null?"—":e.bytes.get()/1024+" KiB");
  put("Video decoder",e==null?"Not started":e.videoDecoder);
  UsageDelivery.Snapshot delivery=UsageDelivery.snapshot();
  put("Relay listener",e!=null?"Active console relay":idle!=null?"Listening while app is open":IdleUsageRelay.listenerError.isEmpty()?"Stopped":IdleUsageRelay.listenerError);
  put("Usage delivery",delivery.status);
  put("Reports received",Long.toString(delivery.received));put("Reports rejected",Long.toString(delivery.rejected));
  put("Reports confirmed",Long.toString(delivery.confirmed));
  put("Last HTTP response",delivery.http==0?"No response yet":Integer.toString(delivery.http));
  put("Last transport error",delivery.error.isEmpty()?"None":delivery.error);
  put("Pixel conversion",e==null?"—":e.scaleNative?"Native":"Java fallback");
 }
}
