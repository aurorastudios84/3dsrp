package com.aurora.relay;

import android.app.*;
import android.os.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.Typeface;
import android.view.*;
import android.widget.*;
import android.text.InputType;
import android.util.Base64;
import java.math.BigInteger;
import java.net.*;
import java.util.*;

public final class MainActivity extends Activity {
 static final int HOME=0,LIVE=1,CONSOLES=2,SETTINGS=3,DIAGNOSTICS=4,SETUP=5;
 static final int INK=0xffeff5ff,MUTED=0xffafc0d7,BLUE=0xff619fff,GREEN=0xff83e0ad,AMBER=0xffeacb83;
 final Handler ui=new Handler(Looper.getMainLooper());
 final int[] sizeIds={View.generateViewId(),View.generateViewId(),View.generateViewId()};
 ConsoleStore store;ViewFlipper pages;int screen=HOME;
 Spinner consoles,homeConnection,resolution,framerate;RadioGroup quality;EditText bitrate;
 TextView homeRouteLabel,phone,homeStatus,liveTitle,liveStatus,consoleIndicator,handheldIndicator,handheldHint,allAddresses,diagnosticStatus,qualityHint;
 Button start,stop,add,apply,defaults,test;LinearLayout consoleList;
 RelayPreview preview;DiagnosticsPanel diagnostics;SetupWizard wizard;
 boolean refreshing,starting,powerBusy,automaticCloseDialog;AlertDialog closeDialog;
 String pendingPin="",pinConsole="";
 SharedPreferences prefs(){return getPreferences(MODE_PRIVATE);}
 int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
 android.graphics.drawable.GradientDrawable surface(int color){android.graphics.drawable.GradientDrawable d=new android.graphics.drawable.GradientDrawable();d.setColor(color);d.setCornerRadius(dp(16));return d;}
 TextView text(LinearLayout parent,String value,int size){TextView v=new TextView(this);v.setText(value);v.setTextSize(size);v.setTextColor(INK);v.setPadding(0,dp(4),0,dp(8));parent.addView(v,new LinearLayout.LayoutParams(-1,-2));return v;}
 TextView muted(LinearLayout parent,String value,int size){TextView v=text(parent,value,size);v.setTextColor(MUTED);return v;}
 void heading(LinearLayout parent,String title,String hint){TextView t=text(parent,title,25);t.setTypeface(null,Typeface.BOLD);if(!hint.isEmpty())muted(parent,hint,14);}
 LinearLayout column(LinearLayout parent){LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);box.setPadding(dp(16),dp(14),dp(16),dp(14));box.setBackground(surface(0xff0f1d35));LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(12);parent.addView(box,lp);return box;}
 Button button(LinearLayout parent,String title){Button b=new Button(this);b.setText(title);b.setAllCaps(false);b.setTextSize(15);b.setMinHeight(dp(48));b.setTextColor(INK);b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xff1a3050));LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(6);parent.addView(b,lp);return b;}
 Button primary(LinearLayout parent,String title){Button b=button(parent,title);b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xff2768ba));return b;}
 EditText field(LinearLayout parent,String title,String value){TextView label=text(parent,title,14);EditText v=new EditText(this);v.setId(View.generateViewId());label.setLabelFor(v.getId());v.setSingleLine(true);v.setText(value);v.setTextColor(INK);v.setHintTextColor(MUTED);v.setMinHeight(dp(48));parent.addView(v,new LinearLayout.LayoutParams(-1,-2));return v;}
 Spinner spinner(LinearLayout parent,String[] values){Spinner s=new Spinner(this);s.setMinimumHeight(dp(48));ArrayAdapter<String> a=new ArrayAdapter<>(this,android.R.layout.simple_spinner_item,values);a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);s.setAdapter(a);parent.addView(s,new LinearLayout.LayoutParams(-1,-2));return s;}
 LinearLayout page(){ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setClipToPadding(false);LinearLayout body=new LinearLayout(this);body.setOrientation(LinearLayout.VERTICAL);body.setPadding(dp(20),dp(12),dp(20),dp(28));scroll.addView(body);pages.addView(scroll,new android.widget.FrameLayout.LayoutParams(-1,-1));return body;}
 ConsoleStore.Console selected(){int n=consoles.getSelectedItemPosition();return n>=0&&n<store.consoles.size()?store.consoles.get(n):null;}
 ConsoleStore.Console byId(String id){for(ConsoleStore.Console c:store.consoles)if(c.id.equals(id))return c;return null;}
 int sizeIndex(){for(int n=0;n<3;n++)if(quality.getCheckedRadioButtonId()==sizeIds[n])return n;return 1;}
 boolean busy(){return RelayService.active()||starting||powerBusy;}
 void message(String value){RelayService.lastMessage=value;if(homeStatus!=null)homeStatus.setText(value);}
 void hideKeyboard(){View v=getCurrentFocus();if(v!=null)((android.view.inputmethod.InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(v.getWindowToken(),0);}
 void toast(String value){Toast.makeText(this,value,Toast.LENGTH_SHORT).show();}
 void browse(String url){try{startActivity(new Intent(Intent.ACTION_VIEW,android.net.Uri.parse(url)));}catch(ActivityNotFoundException e){toast("No web browser is available on this phone.");}}
 @Override public void onCreate(Bundle saved){super.onCreate(saved);
  try{store=new ConsoleStore(this,prefs());}catch(Exception ex){TextView error=new TextView(this);error.setText("Could not load saved consoles: "+ex.getMessage());error.setTextColor(INK);error.setPadding(dp(24),dp(40),dp(24),dp(24));setContentView(error);return;}
  if(!prefs().getBoolean("source_defaults_v32",false))prefs().edit().putInt("source_height_magnify",StreamProfile.DEFAULT_HEIGHT).putInt("source_bitrate",StreamProfile.DEFAULT_BITRATE).putInt("source_fps",StreamProfile.DEFAULT_FPS).putBoolean("source_defaults_v32",true).apply();
  getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE|WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
  LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(0xff090f20);setContentView(root);
  LinearLayout bar=new LinearLayout(this);bar.setGravity(Gravity.CENTER_VERTICAL);bar.setPadding(dp(20),dp(12),dp(16),dp(8));root.addView(bar);
  TextView brand=new TextView(this);brand.setText("3DSRP Companion");brand.setTextSize(21);brand.setTypeface(null,Typeface.BOLD);brand.setTextColor(INK);bar.addView(brand,new LinearLayout.LayoutParams(0,-2,1));
  Button menu=new Button(this);menu.setText("Menu");menu.setAllCaps(false);menu.setTextColor(INK);menu.setContentDescription("Open navigation menu");menu.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xff172943));bar.addView(menu,new LinearLayout.LayoutParams(-2,dp(48)));menu.setOnClickListener(this::showMenu);
  LinearLayout address=new LinearLayout(this);address.setOrientation(LinearLayout.VERTICAL);address.setPadding(dp(16),dp(10),dp(16),dp(10));address.setBackground(surface(0xff122a47));LinearLayout.LayoutParams addressParams=new LinearLayout.LayoutParams(-1,-2);addressParams.setMargins(dp(20),dp(4),dp(20),dp(4));root.addView(address,addressParams);
  TextView addressLabel=muted(address,"PHONE ADDRESS",11);addressLabel.setLetterSpacing(.12f);addressLabel.setPadding(0,0,0,0);
  LinearLayout ipRow=new LinearLayout(this);ipRow.setGravity(Gravity.CENTER_VERTICAL);address.addView(ipRow);
  phone=new TextView(this);phone.setTextSize(27);phone.setTypeface(Typeface.MONOSPACE,Typeface.BOLD);phone.setTextColor(INK);phone.setTextIsSelectable(true);phone.setMaxLines(1);phone.setAutoSizeTextTypeUniformWithConfiguration(16,27,1,android.util.TypedValue.COMPLEX_UNIT_SP);phone.setContentDescription("Phone IP address to enter on your 3DS");ipRow.addView(phone,new LinearLayout.LayoutParams(0,-2,1));
  TextView addressHint=muted(address,"Enter this address on your 3DS.",12);addressHint.setPadding(0,0,0,0);
  pages=new ViewFlipper(this);root.addView(pages,new LinearLayout.LayoutParams(-1,0,1));
  buildHome();buildLive();buildConsoles();buildSettings();buildDiagnostics();pages.addView(new FrameLayout(this));
  reloadConsoles();
  if(saved!=null&&saved.getInt("screen",HOME)==SETUP)showSetup(byId(saved.getString("wizard_console","")),saved.getInt("wizard_step",0),saved);
  else showPage(saved==null?HOME:saved.getInt("screen",HOME));
  if(getIntent().getBooleanExtra("selftest",false))begin(false,true);
  else if(saved==null&&!busy()&&store.consoles.isEmpty()&&!prefs().getBoolean("onboarding_seen_v1",false))showSetup(null,0,null);
 }
 void buildHome(){
  LinearLayout body=page();heading(body,"Ready to play?","Choose your console and picture quality.");
  LinearLayout card=column(body);text(card,"PlayStation",14);consoles=spinner(card,new String[]{});consoles.setContentDescription("Selected console");add=primary(card,"Set up a console");add.setOnClickListener(v->showSetup(null,0,null));
  homeRouteLabel=text(card,"Connection",14);homeConnection=spinner(card,new String[]{"Local / home","Remote / Internet"});homeConnection.setContentDescription("Home connection route");homeConnection.setOnItemSelectedListener(routeListener());
  text(card,"Picture quality",14);quality=new RadioGroup(this);quality.setOrientation(RadioGroup.HORIZONTAL);card.addView(quality);String[] labels={"Low","Balanced","High"};for(int n=0;n<3;n++){RadioButton b=new RadioButton(this);b.setId(sizeIds[n]);b.setText(labels[n]);b.setTextSize(14);b.setTextColor(INK);b.setButtonDrawable((android.graphics.drawable.Drawable)null);b.setGravity(Gravity.CENTER);b.setPadding(dp(4),dp(4),dp(4),dp(4));RadioGroup.LayoutParams lp=new RadioGroup.LayoutParams(0,dp(48),1);lp.setMargins(dp(2),dp(6),dp(2),0);quality.addView(b,lp);}
  quality.check(sizeIds[Math.max(0,Math.min(2,prefs().getInt("image_size",1)))]);qualityHint=muted(card,"",13);quality.setOnCheckedChangeListener((g,id)->{prefs().edit().putInt("image_size",sizeIndex()).apply();updateQuality();});updateQuality();
  start=primary(body,"Connect");start.setOnClickListener(v->begin(false,false));homeStatus=muted(body,"",14);
  consoles.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){public void onNothingSelected(AdapterView<?> p){}public void onItemSelected(AdapterView<?> p,View v,int pos,long id){if(refreshing)return;ConsoleStore.Console c=selected();if(c!=null){store.prefs.edit().putString("selected",c.id).apply();loadRoute();}}});
 }
 void updateQuality(){int selected=sizeIndex();for(int n=0;n<3;n++)quality.getChildAt(n).setBackground(surface(n==selected?0xff2768ba:0xff1a2d49));if(qualityHint!=null)qualityHint.setText(new String[]{"Prioritise smoothness.","Balance smoothness and detail.","Prioritise detail."}[selected]);}
 void buildLive(){
  LinearLayout body=page();liveTitle=text(body,"Connecting…",23);liveTitle.setTypeface(null,Typeface.BOLD);
  preview=new RelayPreview(this);body.addView(preview,new LinearLayout.LayoutParams(-1,-2));
  LinearLayout links=new LinearLayout(this);links.setPadding(0,dp(12),0,dp(4));body.addView(links);
  consoleIndicator=new TextView(this);consoleIndicator.setTextSize(13);consoleIndicator.setTextColor(MUTED);links.addView(consoleIndicator,new LinearLayout.LayoutParams(0,-2,1));
  handheldIndicator=new TextView(this);handheldIndicator.setTextSize(13);handheldIndicator.setTextColor(AMBER);links.addView(handheldIndicator,new LinearLayout.LayoutParams(0,-2,1));
  handheldHint=muted(body,"Open your 3DS app and enter the phone address above.",14);liveStatus=muted(body,"",14);stop=button(body,"Disconnect");stop.setOnClickListener(v->end());
 }
 void buildConsoles(){LinearLayout body=page();heading(body,"Your consoles","Set up or manage your PlayStation connections.");Button addConsole=primary(body,"Add a console");addConsole.setOnClickListener(v->{if(busy())toast("Disconnect before adding a console");else showSetup(null,0,null);});consoleList=new LinearLayout(this);consoleList.setOrientation(LinearLayout.VERTICAL);body.addView(consoleList);}
 void buildSettings(){
  LinearLayout body=page();heading(body,"Stream settings","Changes apply the next time you connect.");
  LinearLayout source=column(body);text(source,"Console input",18);text(source,"Resolution",14);resolution=spinner(source,new String[]{"360p","540p"});resolution.setSelection(prefs().getInt("source_height_magnify",StreamProfile.DEFAULT_HEIGHT)==360?0:1);
  text(source,"Frame rate",14);framerate=spinner(source,new String[]{"30 fps","60 fps"});framerate.setContentDescription("Console source frame rate");framerate.setSelection(prefs().getInt("source_fps",StreamProfile.DEFAULT_FPS)==30?0:1);
  bitrate=field(source,"Bitrate (kbps)",""+prefs().getInt("source_bitrate",StreamProfile.DEFAULT_BITRATE));bitrate.setInputType(InputType.TYPE_CLASS_NUMBER);apply=primary(source,"Save stream settings");apply.setOnClickListener(v->saveSettings());
  defaults=button(body,"Restore default settings");defaults.setOnClickListener(v->{if(busy())return;prefs().edit().putInt("image_size",1).putInt("source_height_magnify",StreamProfile.DEFAULT_HEIGHT).putInt("source_bitrate",StreamProfile.DEFAULT_BITRATE).putInt("source_fps",StreamProfile.DEFAULT_FPS).apply();quality.check(sizeIds[1]);resolution.setSelection(0);framerate.setSelection(0);bitrate.setText(""+StreamProfile.DEFAULT_BITRATE);toast("360p · 30 fps defaults restored");});
 }
 void saveSettings(){if(busy())return;try{int height=resolution.getSelectedItemPosition()==0?360:540,fps=framerate.getSelectedItemPosition()==0?30:60,rate=StreamProfile.parseBitrate(bitrate.getText().toString());new StreamProfile(height,rate,fps);prefs().edit().putInt("source_height_magnify",height).putInt("source_fps",fps).putInt("source_bitrate",rate).apply();bitrate.setError(null);hideKeyboard();toast("Stream settings saved");}catch(Exception e){bitrate.setError(e.getMessage());bitrate.requestFocus();}}
 void buildDiagnostics(){LinearLayout body=page();diagnostics=new DiagnosticsPanel(this);body.addView(diagnostics);LinearLayout address=column(body);text(address,"Phone network addresses",16);allAddresses=muted(address,"",14);allAddresses.setTextIsSelectable(true);diagnosticStatus=muted(body,"",14);test=button(body,"Test connection to 3DS");test.setOnClickListener(v->begin(true,false));}
 AdapterView.OnItemSelectedListener routeListener(){return new AdapterView.OnItemSelectedListener(){
  public void onNothingSelected(AdapterView<?> parent){}
  public void onItemSelected(AdapterView<?> parent,View view,int route,long id){
   ConsoleStore.Console c=selected();if(refreshing||busy()||c==null||!c.id.equals(parent.getTag())||parent.getSelectedItemPosition()!=route)return;
   store.prefs.edit().putInt("route-"+c.id,route).apply();
  }
 };}
 void loadRoute(){ConsoleStore.Console c=selected();if(c==null||homeConnection==null)return;homeConnection.setTag(c.id);homeConnection.setSelection(store.prefs.getInt("route-"+c.id,0));}
 void reloadConsoles(){
  refreshing=true;ArrayAdapter<ConsoleStore.Console> a=new ArrayAdapter<>(this,android.R.layout.simple_spinner_item,store.consoles);a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);consoles.setAdapter(a);String id=store.prefs.getString("selected","");int index=0;for(int n=0;n<store.consoles.size();n++)if(store.consoles.get(n).id.equals(id))index=n;if(!store.consoles.isEmpty())consoles.setSelection(index);loadRoute();refreshing=false;consoles.setVisibility(store.consoles.isEmpty()?View.GONE:View.VISIBLE);homeRouteLabel.setVisibility(store.consoles.isEmpty()?View.GONE:View.VISIBLE);homeConnection.setVisibility(store.consoles.isEmpty()?View.GONE:View.VISIBLE);add.setVisibility(store.consoles.isEmpty()?View.VISIBLE:View.GONE);start.setVisibility(store.consoles.isEmpty()?View.GONE:View.VISIBLE);renderConsoleList();
 }
 void renderConsoleList(){consoleList.removeAllViews();for(ConsoleStore.Console c:store.consoles){LinearLayout card=column(consoleList);text(card,c.name,20);muted(card,(c.ps5?"PS5":"PS4")+" · "+c.local,14);Button edit=button(card,"Edit setup / pair again");edit.setOnClickListener(v->showSetup(c,0,null));Button internet=button(card,"Internet address");internet.setOnClickListener(v->editInternet(c));Button remove=button(card,"Remove console");remove.setOnClickListener(v->removeConsole(c));}}
 void editInternet(ConsoleStore.Console c){if(busy()){toast("Disconnect before changing a console");return;}LinearLayout form=new LinearLayout(this);form.setOrientation(LinearLayout.VERTICAL);form.setPadding(dp(24),dp(8),dp(24),dp(8));EditText address=field(form,"Internet IP or hostname",c.internet);muted(form,"Optional. Your router must forward Remote Play ports to this console.",14);AlertDialog d=new AlertDialog.Builder(this).setTitle(c.name).setView(form).setNegativeButton("Cancel",null).setPositiveButton("Save",null).create();d.setOnShowListener(x->d.getButton(-1).setOnClickListener(v->{String old=c.internet;try{c.internet=validHost(address.getText().toString(),true);store.save();d.dismiss();toast("Internet address saved");}catch(Exception e){c.internet=old;address.setError(e.getMessage());}}));d.show();}
 boolean paired(ConsoleStore.Console c){try{return RelayEngine.nativeHasPair(c.local,parseAccount(c.account),store.pairFile(c).getAbsolutePath(),c.ps5);}catch(Exception ex){return false;}}
 void showSetup(ConsoleStore.Console c,int step,Bundle restored){if(busy()&&c==null)c=selected();wizard=new SetupWizard(this,c,step,restored);pages.removeViewAt(SETUP);pages.addView(wizard,SETUP);showPage(SETUP);}
 ConsoleStore.Console saveConsole(ConsoleStore.Console existing,String name,String local,String account,boolean ps5,String pin)throws Exception{
  if(busy())throw new IllegalStateException("Disconnect before changing console setup");
  ConsoleStore.Console c=new ConsoleStore.Console(existing==null?UUID.randomUUID().toString():existing.id,name,local,existing==null?"":existing.internet,account,ps5);int index=existing==null?store.consoles.size():store.consoles.indexOf(existing);if(existing==null)store.consoles.add(c);else store.consoles.set(index,c);
  try{store.save();}catch(Exception e){if(existing==null)store.consoles.remove(c);else store.consoles.set(index,existing);throw e;}
  store.prefs.edit().putString("selected",c.id).putInt("route-"+c.id,0).apply();pendingPin=pin;pinConsole=c.id;prefs().edit().putBoolean("onboarding_seen_v1",true).apply();reloadConsoles();return c;
 }
 void finishGuide(){prefs().edit().putBoolean("onboarding_seen_v1",true).apply();showPage(HOME);}
 void removeConsole(ConsoleStore.Console c){if(busy()){toast("Disconnect before removing a console");return;}new AlertDialog.Builder(this).setTitle("Remove "+c.name+"?").setMessage("Its saved connection and pairing will be removed.").setNegativeButton("Cancel",null).setPositiveButton("Remove",(d,w)->{try{int i=store.consoles.indexOf(c);store.consoles.remove(i);try{store.save();}catch(Exception e){store.consoles.add(i,c);throw e;}java.nio.file.Files.deleteIfExists(store.pairFile(c).toPath());if(pinConsole.equals(c.id)){pinConsole="";pendingPin="";}reloadConsoles();}catch(Exception e){message(e.getMessage());}}).show();}
 void showMenu(View anchor){PopupMenu menu=new PopupMenu(this,anchor);menu.getMenu().add(0,HOME,0,busy()?"Live picture":"Home");menu.getMenu().add(0,CONSOLES,1,"Your consoles");menu.getMenu().add(0,SETTINGS,2,"Stream settings");menu.getMenu().add(0,DIAGNOSTICS,3,"Diagnostics");menu.getMenu().add(0,SETUP,4,"How to connect");if(RelayService.active())menu.getMenu().add(0,10,5,"Disconnect");menu.setOnMenuItemClickListener(item->{int id=item.getItemId();if(id==10)end();else if(id==SETUP)showSetup(selected(),0,null);else showPage(id);return true;});menu.show();}
 void showPage(int next){hideKeyboard();if(next<HOME||next>SETUP)next=HOME;if(next==HOME&&busy())next=LIVE;screen=next;pages.setDisplayedChild(next);if(next==DIAGNOSTICS&&diagnostics!=null)diagnostics.update(RelayService.session());if(preview!=null&&next!=LIVE)preview.update(null);}
 void begin(boolean testMode,boolean fixture){if(busy())return;try{
  ConsoleStore.Console c=selected();if(!testMode&&!fixture&&c==null){showSetup(null,0,null);return;}
  int height=prefs().getInt("source_height_magnify",StreamProfile.DEFAULT_HEIGHT),rate=prefs().getInt("source_bitrate",StreamProfile.DEFAULT_BITRATE),fps=prefs().getInt("source_fps",StreamProfile.DEFAULT_FPS);if(fixture)fps=getIntent().getIntExtra("source_fps",fps);new StreamProfile(height,rate,fps);
  Intent i=new Intent(this,RelayService.class).putExtra("test",testMode).putExtra("fixture",fixture).putExtra("height",height).putExtra("bitrate",rate).putExtra("fps",fps).putExtra("size",fixture?getIntent().getIntExtra("image_size",1):sizeIndex());
  if(!testMode&&!fixture){String host=store.prefs.getInt("route-"+c.id,0)==1?c.internet:c.local;if(host.isEmpty())throw new IllegalArgumentException("Add this console’s Internet address in Menu → Your consoles");String code=pinConsole.equals(c.id)?pendingPin:"";if(!paired(c)&&code.isEmpty()){showSetup(c,4,null);return;}
   i.putExtra("label",c.name).putExtra("consoleId",c.id).putExtra("host",host).putExtra("account",parseAccount(c.account)).putExtra("pin",code).putExtra("pairFile",store.pairFile(c).getAbsolutePath()).putExtra("ps5",c.ps5);
  }else i.putExtra("label",fixture?"Codec test":"Connection test");
  prefs().edit().putInt("source_height_magnify",height).putInt("source_bitrate",rate).putInt("source_fps",fps).apply();
  if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},1);
  starting=true;startForegroundService(i);pendingPin="";message("Connecting…");showPage(LIVE);ui.postDelayed(()->starting=false,1500);
 }catch(Exception e){starting=false;message(e.getMessage());showPage(HOME);}}
 void stopChoice(){RelayService service=RelayService.current;if(service==null||service.stopping||closeDialog!=null)return;
  if(!service.consoleSession||!service.connected){disconnect(false);return;}
  automaticCloseDialog=service.awaitingCloseChoice;closeDialog=new AlertDialog.Builder(this).setTitle("Stop relay").setMessage("Leave the console on, or put it into Rest Mode?").setNegativeButton("Cancel",(d,w)->{if(service.awaitingCloseChoice)disconnect(false);}).setNeutralButton("Leave console on",(d,w)->disconnect(false)).setPositiveButton("Put console in Rest Mode",(d,w)->disconnect(true)).create();closeDialog.setOnDismissListener(d->closeDialog=null);closeDialog.show();
 }
 void disconnect(boolean rest){startService(new Intent(this,RelayService.class).setAction(rest?"REST":"STOP"));message(rest?"Requesting Rest Mode…":"Stopping relay…");}
 void end(){if(RelayService.active())stopChoice();}
 String addresses(){LinkedHashSet<String> ips=new LinkedHashSet<>();try{
  android.net.ConnectivityManager cm=(android.net.ConnectivityManager)getSystemService(CONNECTIVITY_SERVICE);
  for(android.net.Network network:cm.getAllNetworks()){android.net.NetworkCapabilities caps=cm.getNetworkCapabilities(network);if(caps==null||(!caps.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI)&&!caps.hasTransport(android.net.NetworkCapabilities.TRANSPORT_ETHERNET)))continue;android.net.LinkProperties properties=cm.getLinkProperties(network);if(properties!=null)for(android.net.LinkAddress a:properties.getLinkAddresses())if(a.getAddress() instanceof Inet4Address)ips.add(a.getAddress().getHostAddress());}
  Enumeration<NetworkInterface> interfaces=NetworkInterface.getNetworkInterfaces();while(interfaces.hasMoreElements()){NetworkInterface n=interfaces.nextElement();String name=n.getName().toLowerCase(Locale.ROOT);if(!n.isUp()||n.isLoopback()||!(name.contains("wlan")||name.startsWith("ap")||name.contains("swlan")||name.contains("softap")||name.startsWith("eth")))continue;Enumeration<InetAddress> aa=n.getInetAddresses();while(aa.hasMoreElements()){InetAddress a=aa.nextElement();if(a instanceof Inet4Address&&!a.isLoopbackAddress())ips.add(a.getHostAddress());}}
 }catch(Exception ignored){}return ips.isEmpty()?"Connect Wi-Fi or enable hotspot":android.text.TextUtils.join("\n",ips);}
 static byte[] parseAccount(String text){text=text.trim();if(text.matches("[0-9]+")){BigInteger n=new BigInteger(text);if(n.signum()<0||n.bitLength()>64)throw new IllegalArgumentException("Account ID is outside unsigned 64-bit range");byte[] b=new byte[8];for(int i=0;i<8;i++)b[i]=n.shiftRight(i*8).byteValue();return b;}byte[] b=Base64.decode(text,Base64.DEFAULT);if(b.length!=8)throw new IllegalArgumentException("Account ID must decode to exactly 8 bytes");return b;}
 static String validHost(String value,boolean optional){String h=value.trim();if(optional&&h.isEmpty())return h;if(h.isEmpty()||h.length()>63||!h.matches("[A-Za-z0-9][A-Za-z0-9.-]*"))throw new IllegalArgumentException("Enter an IP or hostname without a URL or port");return h;}
 String primaryAddress(){String first=addresses().split("\n")[0];return first.matches("[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+")?first:"";}
 void updateSession(){
  if(!RelayService.active()&&!starting)IdleUsageRelay.start();
  RelayService service=RelayService.current;RelayEngine e=RelayService.session();boolean active=busy(),source=service!=null&&service.connected&&!service.stopping,handheld=e!=null&&e.hasPeer()&&service!=null&&!service.stopping;
  if(service!=null&&service.awaitingCloseChoice)stopChoice();else if(closeDialog!=null&&(service==null||(automaticCloseDialog&&!service.awaitingCloseChoice)))closeDialog.dismiss();
  if(active&&screen==HOME)showPage(LIVE);else if(!active&&screen==LIVE)showPage(HOME);
  String ip=primaryAddress();phone.setText(ip.isEmpty()?"Connect to Wi-Fi":ip);phone.setTextSize(ip.isEmpty()?19:27);allAddresses.setText(addresses());
  consoles.setEnabled(!active);add.setEnabled(!active);start.setEnabled(!active);stop.setEnabled(RelayService.active());test.setEnabled(!active);defaults.setEnabled(!active);apply.setEnabled(!active);resolution.setEnabled(!active);framerate.setEnabled(!active);bitrate.setEnabled(!active);homeConnection.setEnabled(!active);for(int n=0;n<quality.getChildCount();n++)quality.getChildAt(n).setEnabled(!active);
  String last=RelayService.lastMessage;homeStatus.setText(last);homeStatus.setVisibility(last==null||last.equals("Ready")||last.equals("Ready to start")?View.GONE:View.VISIBLE);homeStatus.setTextColor(last!=null&&(last.contains("failed")||last.startsWith("Disconnected")||last.contains("error"))?0xffffaaaa:MUTED);
  liveTitle.setText(service==null?"Connecting…":service.label==null?"Live picture":service.label);
  boolean testing=service!=null&&!service.consoleSession;consoleIndicator.setText(testing?"Connection test":source?"●  PlayStation connected":"●  Connecting to PlayStation");consoleIndicator.setTextColor(source||testing?GREEN:AMBER);
  String mismatch=e==null?"":e.compatibilityError;handheldIndicator.setText(!mismatch.isEmpty()?"●  3DS version mismatch":handheld?"●  3DS connected":"●  3DS not connected");handheldIndicator.setTextColor(handheld?GREEN:AMBER);
  handheldHint.setVisibility(handheld?View.GONE:View.VISIBLE);handheldHint.setText(!mismatch.isEmpty()?"Update both apps to the same release. See Diagnostics.":"Open your 3DS app and enter the phone address above.");
  liveStatus.setText(source||testing?"":last);liveStatus.setVisibility(source||testing?View.GONE:View.VISIBLE);
  if(screen==DIAGNOSTICS){diagnostics.update(e);diagnosticStatus.setText(last);}
  if(screen==LIVE)preview.update(e);else preview.update(null);
 }
 long lastUiUpdate;
 final Runnable tick=new Runnable(){public void run(){long now=SystemClock.elapsedRealtime();if(now-lastUiUpdate>=1000){lastUiUpdate=now;updateSession();}else if(screen==LIVE)preview.update(RelayService.session());ui.postDelayed(this,100);}};
 @Override public void onStart(){super.onStart();if(store!=null){lastUiUpdate=0;ui.post(tick);}}
 @Override public void onStop(){IdleUsageRelay.stop();if(preview!=null)preview.update(null);ui.removeCallbacks(tick);super.onStop();}
 @Override public void onSaveInstanceState(Bundle state){super.onSaveInstanceState(state);state.putInt("screen",screen);if(screen==SETUP&&wizard!=null)wizard.save(state);}
 @Override public void onBackPressed(){if(screen==SETUP&&wizard!=null&&wizard.back())return;if(screen!=HOME&&screen!=LIVE){showPage(HOME);return;}super.onBackPressed();}
 @Override public void onDestroy(){ui.removeCallbacksAndMessages(null);super.onDestroy();}
}
