package com.aurora.relay;

import android.os.Bundle;
import android.text.InputType;
import android.view.View;
import android.widget.*;

/** Small, resumable steps. The pairing PIN remains transient and is requested last. */
final class SetupWizard extends LinearLayout {
 static final String SONY="https://www.playstation.com/en-us/support/games/playstation-remote-play-on-mobile-devices/";
 static final String PAIRING="https://www.playstation.com/en-us/support/games/remote-play-settings/";
 static final String CHIAKI="https://chiaki-ng.github.io/chiaki-ng/setup/configuration/";
 static final String LOOKUP="https://psn.flipscreen.games/";
 final MainActivity a;final LinearLayout body,actions;final ScrollView scroll;final boolean readOnly;
 ConsoleStore.Console existing,savedConsole;int step;boolean ps5=true;
 String name="My PS5",local="",account="",pin="";Spinner type;EditText nameField,ipField,accountField,pinField;TextView error;
 SetupWizard(MainActivity a,ConsoleStore.Console existing,int step,Bundle restored){
  super(a);this.a=a;this.existing=existing;this.step=Math.max(0,Math.min(5,step));readOnly=a.busy();setOrientation(VERTICAL);
  if(existing!=null){name=existing.name;local=existing.local;account=existing.account;ps5=existing.ps5;}
  if(restored!=null){name=restored.getString("wizard_name",name);local=restored.getString("wizard_ip",local);account=restored.getString("wizard_account",account);ps5=restored.getBoolean("wizard_ps5",ps5);}
  body=new LinearLayout(a);body.setOrientation(LinearLayout.VERTICAL);body.setPadding(a.dp(20),a.dp(12),a.dp(20),a.dp(16));scroll=new ScrollView(a);scroll.addView(body);addView(scroll,new LinearLayout.LayoutParams(-1,0,1));actions=new LinearLayout(a);actions.setPadding(a.dp(16),a.dp(6),a.dp(16),a.dp(12));addView(actions,new LinearLayout.LayoutParams(-1,-2));render();
 }
 void render(){
  body.removeAllViews();actions.removeAllViews();type=null;nameField=ipField=accountField=pinField=null;
  if(step==5){complete();return;}
  TextView progress=a.muted(body,"STEP "+(step+1)+" OF 5",11);progress.setLetterSpacing(.1f);
  switch(step){
   case 0:
    a.heading(body,"Let’s connect your console","We’ll walk through the details together.");
    a.muted(body,"Use the same home network for your PlayStation, Android phone and 3DS. Turn on the console and sign in to the user you want to play as.",15);
    LinearLayout intro=a.column(body);a.text(intro,"Your console",14);type=a.spinner(intro,new String[]{"PS5","PS4"});type.setContentDescription("Console type");type.setSelection(ps5?0:1);type.setEnabled(true);
    nameField=a.field(intro,"Give it a name",name);nameField.setEnabled(!readOnly);break;
   case 1:
    a.heading(body,"Enable Remote Play",ps5?"On your PS5":"On your PS4");
    route("Open Settings",ps5?"System → Remote Play":"Remote Play Connection Settings");
    a.text(body,"Turn on Enable Remote Play.",17);
    if(!ps5)a.muted(body,"Also open Settings → Account Management → Activate as Your Primary PS4, then select Activate.",15);
    Button wake=a.button(body,"Allow wake from Rest Mode");wake.setOnClickListener(v->{String path=ps5?"Settings → System → Power Saving → Features Available in Rest Mode":"Settings → Power Save Settings → Set Features Available in Rest Mode";new android.app.AlertDialog.Builder(a).setTitle("Wake your "+(ps5?"PS5":"PS4")).setMessage(path+"\n\nTurn on Stay Connected to the Internet and Enable Turning on "+(ps5?"PS5":"PS4")+" from Network.").setPositiveButton("Got it",null).show();});
    source("PlayStation setup instructions",SONY);break;
   case 2:
    a.heading(body,"Find the console’s IP",ps5?"On your PS5":"On your PS4");
    route("Open Settings",ps5?"System → System Software → Console Information":"Network → View Connection Status");
    a.muted(body,"Copy the IPv4 address shown there, for example 192.168.1.100. This is the console’s address; the blue card above shows your phone’s address for the 3DS.",15);
    ipField=a.field(a.column(body),"Console IP address or hostname",local);ipField.setHint("192.168.1.100");ipField.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_URI);ipField.setEnabled(!readOnly);break;
   case 3:
    a.heading(body,"Get your PSN Account ID","The same process works for PS5 and PS4.");
    a.muted(body,"The Account ID is not displayed in console settings. Open your console profile and note your PSN Online ID, then use the lookup below.",15);
    LinearLayout steps=a.column(body);a.text(steps,"1. Enter your PSN Online ID on the lookup site.\n\n2. Copy Encoded ID (for Chiaki).\n\n3. Return here and paste it below.",15);
    Button lookup=a.primary(steps,"Find my Account ID");lookup.setOnClickListener(v->a.browse(LOOKUP));a.muted(steps,"Opens FlipScreen Games in your browser. It looks up your public PSN name. If the lookup cannot find you, check that your PSN search privacy allows Anyone, or use Chiaki’s PSN sign-in method.",12);
    accountField=a.field(body,"PSN Account ID",account);accountField.setHint("Paste Encoded ID (for Chiaki)");accountField.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);accountField.setEnabled(!readOnly);a.muted(body,"A numeric Account ID also works. Use the ID for the console user you signed in as.",13);source("Other ways to find your Account ID",CHIAKI);break;
   case 4:
    a.heading(body,"Link this phone",ps5?"Get a fresh code from your PS5":"Get a fresh code from your PS4");
    route("Open Settings",ps5?"System → Remote Play → Link Device":"Remote Play Connection Settings → Add Device");
    a.muted(body,"Enter the eight-digit code displayed by the console. Keep that screen open until you connect.",15);
    if(!readOnly){pinField=a.field(a.column(body),"Link Device PIN",pin);pinField.setHint("8-digit pairing code");pinField.setSaveEnabled(false);pinField.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);pinField.setInputType(InputType.TYPE_CLASS_NUMBER|InputType.TYPE_NUMBER_VARIATION_PASSWORD);a.muted(body,"Already paired? Leave this blank to keep the saved pairing. This is the Link Device code, not your console login passcode. The code is not saved.",13);}
    else a.muted(body,"You are already running a session. Disconnect first if you need to pair again.",14);
    source("PlayStation pairing instructions",PAIRING);break;
  }
  error=a.text(body,"",14);error.setTextColor(0xffffaaaa);error.setVisibility(View.GONE);error.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
  Button back=a.button(actions,step==0?"Later":"Back");LinearLayout.LayoutParams backParams=new LinearLayout.LayoutParams(a.dp(88),a.dp(52));backParams.rightMargin=a.dp(8);back.setLayoutParams(backParams);back.setOnClickListener(v->{if(step==0)a.finishGuide();else back();});
  Button next=a.primary(actions,step==4?(readOnly?"Back to live picture":"Save console"):"Next");next.setLayoutParams(new LinearLayout.LayoutParams(0,a.dp(52),1));next.setOnClickListener(v->next());
  scroll.post(()->scroll.scrollTo(0,0));
 }
 void route(String title,String path){LinearLayout card=a.column(body);a.muted(card,title,12);a.text(card,path,17);}
 void source(String label,String url){Button b=a.button(body,label);b.setOnClickListener(v->a.browse(url));}
 void capture(){if(type!=null){ps5=type.getSelectedItemPosition()==0;name=nameField.getText().toString().trim();if(name.equals("My PS5")&&!ps5)name="My PS4";else if(name.equals("My PS4")&&ps5)name="My PS5";}if(ipField!=null)local=ipField.getText().toString().trim();if(accountField!=null)account=accountField.getText().toString().trim();if(pinField!=null)pin=pinField.getText().toString().trim();}
 void next(){capture();try{
  if(readOnly){if(step>=4){a.finishGuide();return;}step++;render();return;}
  if(step==0&&name.isEmpty())throw new IllegalArgumentException("Give this console a name.");
  if(step==2)local=MainActivity.validHost(local,false);
  if(step==3)try{MainActivity.parseAccount(account);}catch(Exception e){throw new IllegalArgumentException("Paste a valid Encoded ID or numeric Account ID.");}
  if(step==4){
   if(readOnly){a.finishGuide();return;}
   MainActivity.validHost(local,false);MainActivity.parseAccount(account);
   ConsoleStore.Console original=savedConsole==null?existing:savedConsole;
   ConsoleStore.Console proposed=new ConsoleStore.Console(original==null?"":original.id,name,local,"",account,ps5);
   if(!pin.isEmpty()&&!pin.matches("[0-9]{8}"))throw new IllegalArgumentException("The Link Device PIN must contain eight digits.");
   if(pin.isEmpty()&&(original==null||!a.paired(proposed)))throw new IllegalArgumentException("Get a fresh Link Device PIN to pair this console.");
   savedConsole=a.saveConsole(original,name,local,account,ps5,pin);existing=savedConsole;
  }
  a.hideKeyboard();step++;render();
 }catch(Exception ex){error.setText(ex.getMessage());error.setVisibility(View.VISIBLE);error.post(()->error.requestRectangleOnScreen(new android.graphics.Rect(0,0,error.getWidth(),error.getHeight()),false));}}
 void complete(){
  a.heading(body,"You’re ready to connect","Your console is saved. Pairing finishes when you connect.");
  LinearLayout card=a.column(body);a.text(card,"1. Tap Connect now.\n\n2. Open the relay app on your 3DS.\n\n3. Enter the phone address shown above.",17);
  a.muted(body,"Next time, just choose your console and quality, then tap Connect. Reopen this guide from Menu → How to connect.",14);
  Button connect=a.primary(actions,"Connect now");connect.setOnClickListener(v->{a.finishGuide();a.begin(false,false);});Button later=a.button(actions,"Go to home");later.setOnClickListener(v->a.finishGuide());connect.setLayoutParams(new LinearLayout.LayoutParams(0,a.dp(52),1));LinearLayout.LayoutParams homeParams=new LinearLayout.LayoutParams(a.dp(110),a.dp(52));homeParams.leftMargin=a.dp(8);later.setLayoutParams(homeParams);
 }
 boolean back(){if(step<=0)return false;capture();a.hideKeyboard();step--;render();return true;}
 void save(Bundle b){capture();b.putInt("wizard_step",step);b.putString("wizard_console",existing==null?"":existing.id);b.putString("wizard_name",name);b.putString("wizard_ip",local);b.putString("wizard_account",account);b.putBoolean("wizard_ps5",ps5);}
}
