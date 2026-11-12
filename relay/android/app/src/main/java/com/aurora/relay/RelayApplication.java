package com.aurora.relay;

import android.app.Application;
import android.content.SharedPreferences;

public final class RelayApplication extends Application {
 @Override public void onCreate(){super.onCreate();
  SharedPreferences prefs=getSharedPreferences("usage-delivery-diagnostics",MODE_PRIVATE);
  UsageDelivery.restore(new UsageDelivery.Store(){
   public UsageDelivery.Snapshot load(){return new UsageDelivery.Snapshot(prefs.getString("status","No usage reports received yet"),prefs.getString("error",""),prefs.getLong("received",0),prefs.getLong("rejected",0),prefs.getLong("confirmed",0),prefs.getLong("failed",0),prefs.getInt("http",0));}
   public void save(UsageDelivery.Snapshot s){prefs.edit().putString("status",s.status).putString("error",s.error).putLong("received",s.received).putLong("rejected",s.rejected).putLong("confirmed",s.confirmed).putLong("failed",s.failed).putInt("http",s.http).apply();}
  });
 }
}
