#!/usr/bin/env python3
"""Exercise production native session outcomes, cleanup generation guards and numeric HTTP reasons."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];source=(r/'android/app/src/main/cpp/bridge.cpp').read_text()
events=source[source.index('static void event_cb('):source.index('static void display_cb(')]
cleanup=source[source.index('static void stop_locked('):source.index('struct Registration ')]
a=source.index('extern "C" JNIEXPORT void JNICALL Java_com_aurora_relay_RelayEngine_nativeStopAttempt');b=source.index('extern "C" JNIEXPORT void JNICALL Java_com_aurora_relay_RelayEngine_nativeStop(',a)
ctrl=(r/'vendor/chiaki-upstream/lib/src/ctrl.c').read_text();a2=ctrl.index('\t\tCHIAKI_LOGE(session->log, "Ctrl http response was not successful.');b2=ctrl.index('\t\tchiaki_http_response_fini(&http_response);',a2)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.cpp').write_text(r'''
#include <chiaki/session.h>
#include <chiaki/http.h>
#include "ctrl_failure.hpp"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <mutex>
#define JNIEXPORT
#define JNICALL
using jobject=void*;using jlong=long long;using jboolean=bool;using jstring=std::string*;
static RelayCtrlFailure ctrl_failure;
static std::atomic<bool> console_connected{false};static std::atomic<uint64_t> connection[3];
static unsigned connected_calls,quit_calls,stops,joins,finis,deletes;static bool retry;static std::string message;
struct JNIEnv {
 void CallVoidMethod(jobject,int){connected_calls++;}
 void CallVoidMethod(jobject,int,jstring text,jboolean can_retry){quit_calls++;message=*text;retry=can_retry;}
 jstring NewStringUTF(const char *text){return new std::string(text);}
 void DeleteLocalRef(jstring text){delete text;}
 void DeleteGlobalRef(jobject){deletes++;}
 bool ExceptionCheck(){return false;}void ExceptionClear(){}
} jni;
struct Env {JNIEnv *e=&jni;};
struct {std::mutex lock;std::atomic<bool> stopping{false};std::atomic<uint64_t> generation{7};jobject owner=(jobject)1;int connected=1,quit=2;ChiakiSession session{};bool initialized=true,started=true;} g;
static void status(const char *text){message=text;}
extern "C" const char *chiaki_quit_reason_string(ChiakiQuitReason){return "Ctrl failed to connect";}
extern "C" ChiakiErrorCode chiaki_session_stop(ChiakiSession*){stops++;return CHIAKI_ERR_SUCCESS;}
extern "C" ChiakiErrorCode chiaki_session_join(ChiakiSession*){joins++;return CHIAKI_ERR_SUCCESS;}
extern "C" void chiaki_session_fini(ChiakiSession*){finis++;}
static void log_error(const char *fmt,...){char text[200];va_list args;va_start(args,fmt);vsnprintf(text,sizeof(text),fmt,args);va_end(args);ctrl_failure.observe(text);}
#undef CHIAKI_LOGE
#define CHIAKI_LOGE(log,...) log_error(__VA_ARGS__)
static void reject(ChiakiHttpResponse http_response){
'''+ctrl[a2:b2]+r'''
}
'''+events+cleanup+source[a:b]+r'''
int main(){
 ChiakiEvent event{};event.type=CHIAKI_EVENT_CONNECTED;event_cb(&event,nullptr);assert(connected_calls==1&&console_connected);
 event.type=CHIAKI_EVENT_QUIT;event.quit.reason=CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED;
 auto outcome=[&](const char *reason,bool expected){ctrl_failure.reset();if(reason)ctrl_failure.observe(reason);event_cb(&event,nullptr);assert(retry==expected);};
 outcome("Ctrl http response was not successful. HTTP code was 403",true);assert(message.find("pair again locally")!=std::string::npos);
 ctrl_failure.observe("Ctrl application reason: 0x80108b15");event_cb(&event,nullptr);assert(!retry&&message.find("Restart the PS5")!=std::string::npos);
 outcome("Ctrl http response was not successful. HTTP code was 401",false);
 outcome("Ctrl connect failed: Timeout",true);
 outcome("Failed to parse ctrl request response",false);
 event.quit.reason=CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;outcome(nullptr,false);assert(message.find("other Remote Play")!=std::string::npos);
 event.quit.reason=CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH;outcome(nullptr,false);
 event.quit.reason=CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;outcome("Reported Application Reason: 0x80108b09 (Regist failed)",false);assert(message.find("fresh Link Device PIN")!=std::string::npos);
 // Parsing uses only the numeric reason; unrelated headers and malformed values are ignored.
 char key[]="rp-application-reason",value[]="80108b02";ChiakiHttpHeader header{};header.key=key;header.value=value;ChiakiHttpResponse response{};response.code=403;response.headers=&header;
 ctrl_failure.reset();reject(response);assert(ctrl_failure.application()==0x80108b02&&!ctrl_failure.retryable());
 for(const char *invalid:{"SECRET","80108b02 SECRET","100000000",""}){header.value=(char*)invalid;ctrl_failure.reset();reject(response);assert(ctrl_failure.application()==0&&ctrl_failure.retryable());}
 header.key=(char*)"RP-Auth";header.value=(char*)"80108b02";ctrl_failure.reset();reject(response);assert(ctrl_failure.application()==0);
 unsigned before=quit_calls;
 Java_com_aurora_relay_RelayEngine_nativeStopAttempt(&jni,nullptr,6);assert(stops==0&&g.owner);
 Java_com_aurora_relay_RelayEngine_nativeStopAttempt(&jni,nullptr,7);assert(stops==1&&joins==1&&finis==1&&deletes==1&&g.owner==nullptr&&!console_connected&&g.generation==7);
 event_cb(&event,nullptr);assert(quit_calls==before); // Shutdown callbacks cannot overwrite an error or trigger retry.
 Java_com_aurora_relay_RelayEngine_nativeStopAttempt(&jni,nullptr,7);assert(stops==1&&joins==1&&finis==1);
}
''')
 subprocess.run(['c++','-std=c++17','-pthread','-fsanitize=address,undefined','-I'+str(r/'common'),'-I'+str(r/'vendor/chiaki-upstream/lib/include'),'-I'+str(r/'android/app/src/main/cpp/generated'),p/'check.cpp','-o',p/'check'],check=True)
 subprocess.run([p/'check'],check=True)
print('PASS native callback HTTP403 vs denial/crash/busy classification; production header reason parser; generation-safe join/finalize; shutdown callback suppression')
