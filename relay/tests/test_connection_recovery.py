#!/usr/bin/env python3
"""Run production startup coordination and status latching under deterministic failure/cancellation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];java=root/'android/app/src/main/java/com/aurora/relay'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'Check.java').write_text(r'''package com.aurora.relay;
import java.util.*;
class Check {
 static class E implements ConsoleConnection.Environment {
  long now;H hooks;ArrayDeque<ConsoleConnection.Event> pending=new ArrayDeque<>();
  public long now(){return now;}
  public ConsoleConnection.Event await(long ms){assert ms>0&&ms<=250;now+=ms;if(hooks.cancelAt>0&&now>=hooks.cancelAt)hooks.live=false;return pending.poll();}
 }
 static class H implements ConsoleConnection.Hooks {
  E env;int starts,cleanups;boolean live=true;long cancelAt;String failure;List<String> statuses=new ArrayList<>();String mode="transient";List<Long> startedAt=new ArrayList<>(),cleanupAt=new ArrayList<>();
  H(E e){env=e;e.hooks=this;}
  public boolean running(){return live;}
  public String start(int attempt){
   assert attempt==++starts;startedAt.add(env.now);
   if(mode.equals("startError"))return "Pairing failed";
   if(mode.equals("silence"))return null;
   if(mode.equals("success")||(mode.equals("transient")&&starts==3)){
    env.pending.add(new ConsoleConnection.Event(true,false,"Console connected."));
    env.pending.add(new ConsoleConnection.Event(false,true,"Disconnected after successful connection"));
   }else env.pending.add(new ConsoleConnection.Event(false,!mode.equals("auth"),"HTTP 403"));
   return null;
  }
  public void cleanup(){cleanupAt.add(env.now);cleanups++;assert cleanups==starts;}
  public void status(String s){statuses.add(s);}
  public void failed(String s){assert failure==null;failure=s;}
 }
 public static void main(String[] args)throws Exception {
  ConsoleConnection c=new ConsoleConnection();E e=new E();H h=new H(e);c.run(h,e);
  assert h.starts==3&&h.cleanups==2&&h.failure.equals("Disconnected after successful connection");
  assert h.statuses.contains("Console connected.")&&e.now>=10_000;
  assert h.startedAt.get(1)-h.cleanupAt.get(0)==5_000&&h.startedAt.get(2)-h.cleanupAt.get(1)==5_000;
  e=new E();h=new H(e);h.mode="permanent403";c.run(h,e);assert h.starts==3&&h.cleanups==2&&h.failure.equals("HTTP 403");
  e=new E();h=new H(e);h.mode="auth";c.run(h,e);assert h.starts==3&&h.cleanups==2&&h.failure.equals("HTTP 403");
  e=new E();h=new H(e);h.mode="startError";c.run(h,e);assert h.starts==1&&h.failure.equals("Pairing failed");
  e=new E();h=new H(e);h.mode="silence";c.run(h,e);assert h.starts==3&&h.cleanups==2&&h.failure.contains("60 seconds")&&e.now==190_000;
  e=new E();h=new H(e);h.cancelAt=1_000;c.run(h,e);assert h.starts==1&&h.cleanups==1&&h.failure==null&&e.now==1_000;
  e=new E();h=new H(e);h.mode="silence";h.cancelAt=1_000;c.run(h,e);assert h.starts==1&&h.failure==null;
  e=new E();h=new H(e);h.mode="success";c.run(h,e);assert h.starts==1&&h.cleanups==0; // Never replay an established session.
  e=new E();h=new H(e);h.live=false;c.run(h,e);assert h.starts==0&&h.failure==null;
  // Native callbacks can arrive during nativeStart, before it returns.
  ConsoleConnection real=new ConsoleConnection();final int[] starts={0};final String[] ended={null};
  real.run(new ConsoleConnection.Hooks(){
   public boolean running(){return true;}public String start(int n){starts[0]++;real.connected();real.quit("ended",true);return null;}
   public void cleanup(){throw new AssertionError();}public void status(String s){}public void failed(String s){ended[0]=s;}
  });assert starts[0]==1&&ended[0].equals("ended");
  List<String> messages=Collections.synchronizedList(new ArrayList<>());SessionStatus state=new SessionStatus(messages::add);
  state.update("Connecting");state.fail("HTTP 403");
  Thread late=new Thread(()->{for(int i=0;i<1000;i++)state.update("Phone video decoder: c2.qti.avc.decoder");});late.start();
  state.fail("later error");late.join();assert state.failure().equals("HTTP 403")&&messages.equals(Arrays.asList("Connecting","HTTP 403"));
  state.close();state.update("Console stopped");state.fail("another error");assert messages.size()==2;
  SessionStatus fresh=new SessionStatus(messages::add);fresh.update("Connecting again");assert fresh.failure()==null&&messages.get(2).equals("Connecting again");
 }
}''')
 subprocess.run(['javac','-d',tmp,java/'ConsoleConnection.java',java/'SessionStatus.java',p/'Check.java'],check=True)
 subprocess.run(['java','-ea','-cp',tmp,'com.aurora.relay.Check'],check=True,timeout=10)
print('PASS three startup attempts with exact 5s gaps; rejection and timeout exhaustion; cancellation/early callbacks; established sessions never replay; terminal error survives concurrent late statuses')
