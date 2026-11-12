#!/usr/bin/env python3
"""Exercise the production network loop and JNI submit path with busy/failure injection."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];java=r/'android/app/src/main/java/com/aurora/relay';cpp=r/'android/app/src/main/cpp'
s=(java/'RelayEngine.java').read_text();network=s[s.index(' void network(){'):s.index(' void decodeVideo(){')]
shim=r'''
package com.aurora.relay;
import java.net.*;import java.nio.*;import java.io.*;import java.util.concurrent.atomic.*;
class SystemClock {static long now;static long elapsedRealtime(){return now;}}
class BuildConfig {static final int VERSION_CODE=24;}
public class WatchdogTest {
 boolean running=true,pattern=false;int session=7,width=240,height=135,clientRun,clientRelease;
 long peerSeen,controlSeen,lastDisplay,lastReport,clientShown,clientDecodeErrors,clientBadFrames;
 boolean hadPeer;int[] cropRequest;String compatibilityError="";AtomicLong fullControls=new AtomicLong();
 UsageProxy usageProxy=new UsageProxy((r,s,v)->{});
 AdaptiveVideoRate videoRate=new AdaptiveVideoRate();
 InetSocketAddress peer;FakeSocket socket;int neutralAttempts,neutralAccepted,remoteL2,linkStates;
 boolean releaseBusy=true;int scenario;
 WatchdogTest(int scenario)throws Exception{this.scenario=scenario;peer=new InetSocketAddress("127.0.0.1",1111);socket=new FakeSocket();}
 void send(int a,int b,int c,int d,int e,int f,int g,int h,byte[] i,int j,int k){}
 void sendDiagnostics(){}void sendLinkState(){linkStates++;}void onStatus(String s){}boolean hasPeer(){return true;}
 boolean nativeControls(int buttons,int lx,int ly,int l2,int r2){
  if(l2!=0){remoteL2=l2;return true;}
  neutralAttempts++;if(releaseBusy&&neutralAttempts<=2)return false;
  neutralAccepted++;remoteL2=0;return true;
 }
 boolean nativeFullControls(byte[] p,int offset){remoteL2=p[offset+12]&255;return true;}
 class FakeSocket {
  int step;
  void send(DatagramPacket p){}
  void receive(DatagramPacket p)throws IOException {
   step++;SystemClock.now+=100;
   byte[] payload=new byte[68];payload[12]=(byte)255;
   byte[] data=Protocol.packet(Protocol.INPUT_FULL,7,1,0,1,68,0,0,1,payload,0,68);
   if(step>1){
    if(scenario==1)data=new byte[10]; // malformed packets: continue path
    else if(scenario==2){} // repeated stale input: continue path
    else if(scenario==3&&step==2)data=Protocol.packet(Protocol.BYE,7,0,0,1,0,0,0,0,null,0,0);
    else if(scenario==4&&step==2)data=Protocol.packet(Protocol.HELLO,0,0,0,1,0,0,0,0,null,0,0); // incompatibility
    else if(scenario==5&&step==2)data=Protocol.packet(Protocol.HELLO,0,Protocol.COMPAT,0,1,0,0,0,123,null,0,0);
    else if(scenario==6&&step==2)data=Protocol.packet(Protocol.PING,7,0,0,1,0,0,0,123,null,0,0);
    else if(scenario>=3)data=new byte[10];
    else {if(step>=14)running=false;throw new SocketTimeoutException();}
   }
   if(step>=14)running=false;
   System.arraycopy(data,0,p.getData(),0,data.length);p.setLength(data.length);p.setAddress(InetAddress.getLoopbackAddress());p.setPort(1111);
  }
 }
'''
main=r'''
 public static void main(String[] args)throws Exception {
  for(int mode=0;mode<7;mode++){
   SystemClock.now=0;WatchdogTest e=new WatchdogTest(mode);e.network();
   if(mode>=5&&e.linkStates!=1)throw new AssertionError("Missing link status heartbeat");
   if(e.neutralAttempts!=3||e.neutralAccepted!=1||e.remoteL2!=0)throw new AssertionError("mode="+mode+" attempts="+e.neutralAttempts+" accepted="+e.neutralAccepted+" value="+e.remoteL2);
  }
 }
}
'''
bridge=(cpp/'bridge.cpp').read_text();a=bridge.index('extern "C" JNIEXPORT jboolean JNICALL Java_com_aurora_relay_RelayEngine_nativeControls');b=bridge.index('extern "C" JNIEXPORT jboolean JNICALL Java_com_aurora_relay_RelayEngine_nativeFullControls',a);native=bridge[a:b]
native_shim=r'''
#include <cassert>
#include <mutex>
#include <thread>
#include <atomic>
#include <chiaki/controller.h>
#define JNIEXPORT
#define JNICALL
#define JNI_TRUE 1
#define JNI_FALSE 0
typedef void JNIEnv;typedef void *jobject;typedef int jint;typedef int jboolean;
struct {std::mutex lock;bool started=true,stopping=false;int session;} g;
static bool fail=false;static unsigned submissions;static ChiakiControllerState received;
extern "C" ChiakiErrorCode chiaki_session_set_controller_state(int *,ChiakiControllerState *c){submissions++;received=*c;return fail?CHIAKI_ERR_UNKNOWN:CHIAKI_ERR_SUCCESS;}
'''
native_main=r'''
int main(){
 std::atomic<bool> ready=false,done=false;
 std::thread busy([&]{std::lock_guard<std::mutex> guard(g.lock);ready=true;while(!done)std::this_thread::yield();});
 while(!ready)std::this_thread::yield();
 assert(!Java_com_aurora_relay_RelayEngine_nativeControls(nullptr,nullptr,0,0,0,0,0));assert(submissions==0);
 done=true;busy.join();fail=true;assert(!Java_com_aurora_relay_RelayEngine_nativeControls(nullptr,nullptr,0,0,0,0,0));
 fail=false;assert(Java_com_aurora_relay_RelayEngine_nativeControls(nullptr,nullptr,0,0,0,0,0));assert(received.l2_state==0&&received.r2_state==0);
 g.started=false;assert(!Java_com_aurora_relay_RelayEngine_nativeControls(nullptr,nullptr,0,0,0,0,0));
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'WatchdogTest.java').write_text(shim+network+main)
 subprocess.run(['javac','-d',tmp,java/'Protocol.java',java/'AdaptiveVideoRate.java',java/'UsageProxy.java',java/'UsageDelivery.java',p/'WatchdogTest.java'],check=True)
 subprocess.run(['java','-cp',tmp,'com.aurora.relay.WatchdogTest'],check=True)
 (p/'check.cpp').write_text(native_shim+native+native_main)
 subprocess.run(['c++','-std=c++17','-pthread','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'vendor/chiaki-upstream/lib/include'),'-I'+str(cpp/'generated'),p/'check.cpp',r/'vendor/chiaki-upstream/lib/src/controller.c','-o',p/'check'],check=True)
 subprocess.run([p/'check'],check=True)
print('PASS production watchdog/JNI: busy submissions retried, timeouts, malformed/stale traffic, disconnect/version rejection and native failure reporting')
