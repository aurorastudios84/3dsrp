package com.aurora.relay;
import android.content.*;
import org.json.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

final class ConsoleStore {
 static final class Console {
  String id,name,local,internet,account;boolean ps5;
  Console(String id,String name,String local,String internet,String account,boolean ps5){this.id=id;this.name=name;this.local=local;this.internet=internet;this.account=account;this.ps5=ps5;}
  public String toString(){return name+" ("+(ps5?"PS5":"PS4")+")";}
 }
 final Context context;final SharedPreferences prefs;final ArrayList<Console> consoles=new ArrayList<>();
 ConsoleStore(Context c,SharedPreferences legacy)throws Exception{
  context=c;prefs=c.getSharedPreferences("consoles",Context.MODE_PRIVATE);
  JSONArray list=new JSONArray(prefs.getString("items","[]"));
  for(int i=0;i<list.length();i++){JSONObject o=list.getJSONObject(i);consoles.add(new Console(o.getString("id"),o.getString("name"),o.getString("local"),o.optString("internet"),o.getString("account"),o.getBoolean("ps5")));}
  if(!prefs.getBoolean("migrated",false)){
   String host=legacy.getString("ip","");if(consoles.isEmpty()&&!host.isEmpty()){
    Console old=new Console(UUID.randomUUID().toString(),"My PS5",host,"",legacy.getString("account",""),true);
    File source=new File(c.getFilesDir(),"ps5-pair.bin");if(source.isFile())Files.copy(source.toPath(),pairFile(old).toPath(),StandardCopyOption.REPLACE_EXISTING);
    consoles.add(old);save();
   }
   if(!prefs.edit().putBoolean("migrated",true).commit())throw new IOException("Could not migrate consoles");
  }
 }
 File pairFile(Console c){return new File(context.getFilesDir(),"console-"+c.id+".bin");}
 void save()throws Exception{
  JSONArray list=new JSONArray();for(Console c:consoles){JSONObject o=new JSONObject();o.put("id",c.id);o.put("name",c.name);o.put("local",c.local);o.put("internet",c.internet);o.put("account",c.account);o.put("ps5",c.ps5);list.put(o);}
  if(!prefs.edit().putString("items",list.toString()).commit())throw new IOException("Could not save consoles");
 }
}
