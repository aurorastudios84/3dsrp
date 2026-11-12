#!/usr/bin/env python3
"""Exercise real wake/Rest Mode JNI functions using fake JNI/network/session APIs; no console is contacted."""
from pathlib import Path
import tempfile,subprocess,shutil
root=Path(__file__).resolve().parents[1];src=(root/'android/app/src/main/cpp/bridge.cpp').read_text();a=src.index('struct Pair ');b=src.index('extern "C" JNIEXPORT jboolean JNICALL Java_com_aurora_relay_RelayEngine_nativeRest',a);end=src.index('extern "C" JNIEXPORT jstring JNICALL Java_com_aurora_relay_RelayEngine_nativeStart',b)
jdk=Path(shutil.which('javac')).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.cpp').write_text('''#include <jni.h>
#include <chiaki/session.h>
#include <chiaki/discovery.h>
#include <chiaki/regist.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <mutex>
#include <atomic>
#include <unistd.h>
static unsigned wakes,rests;static bool expect_ps5=true;
static std::atomic<bool> console_connected{false};
struct {std::mutex lock;bool started=false;std::atomic<bool> stopping{false};ChiakiSession session{};} g;
extern "C" ChiakiErrorCode chiaki_lib_init(){return CHIAKI_ERR_SUCCESS;}
extern "C" void chiaki_log_init(ChiakiLog*,uint32_t,ChiakiLogCb,void*){}
extern "C" ChiakiErrorCode chiaki_discovery_wakeup(ChiakiLog*,ChiakiDiscovery*,const char *host,uint64_t key,bool ps5){assert(!strcmp(host,"home.example.com"));assert(key==0x1234ab&&ps5==expect_ps5);wakes++;return CHIAKI_ERR_SUCCESS;}
extern "C" ChiakiErrorCode chiaki_session_goto_bed(ChiakiSession*){rests++;console_connected=false;return CHIAKI_ERR_SUCCESS;}
'''+src[a:b]+src[b:end]+r'''
static jsize length(JNIEnv*,jarray){return 8;}
static const char *chars(JNIEnv*,jstring s,jboolean*){return (const char*)s;}
static void release(JNIEnv*,jstring,const char*){}
static void bytes(JNIEnv*,jbyteArray a,jsize start,jsize count,jbyte *out){memcpy(out,(char*)a+start,count);}
static jstring string(JNIEnv*,const char *s){return (jstring)s;}
int main(){
 JNINativeInterface_ f{};f.GetArrayLength=length;f.GetStringUTFChars=chars;f.ReleaseStringUTFChars=release;f.GetByteArrayRegion=bytes;f.NewStringUTF=string;JNIEnv env{&f};
 uint8_t aid[8]={1};Pair p;p.account[0]=1;p.host.target=CHIAKI_TARGET_PS5_1;memcpy(p.host.rp_regist_key,"1234ab",7);
 auto save=[&](){FILE *file=fopen("pair.bin","wb");assert(file);fwrite(&p,1,sizeof(p),file);fclose(file);};
 auto wake=[&](bool ps5){return Java_com_aurora_relay_RelayEngine_nativeWake(&env,nullptr,(jstring)"home.example.com",(jbyteArray)aid,(jstring)"pair.bin",ps5);};
 assert(wake(true)!=nullptr&&wakes==0);save();assert(wake(true)==nullptr&&wakes==1);
 assert(wake(false)!=nullptr&&wakes==1);p.host.target=CHIAKI_TARGET_PS4_10;expect_ps5=false;save();assert(wake(false)==nullptr&&wakes==2);
 p.host.rp_regist_key[0]='!';save();assert(wake(false)!=nullptr&&wakes==2);
 assert(!Java_com_aurora_relay_RelayEngine_nativeRest(nullptr,nullptr));g.started=true;
 assert(!Java_com_aurora_relay_RelayEngine_nativeRest(nullptr,nullptr));console_connected=true;g.stopping=true;
 assert(!Java_com_aurora_relay_RelayEngine_nativeRest(nullptr,nullptr));g.stopping=false;
 assert(Java_com_aurora_relay_RelayEngine_nativeRest(nullptr,nullptr)&&rests==1);
}
''')
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(jdk/'include'),'-I'+str(jdk/'include/linux'),'-I'+str(root/'vendor/chiaki-upstream/lib/include'),'-I'+str(root/'android/app/src/main/cpp/generated'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
 subprocess.run([p/'check'],cwd=p,check=True)
print('PASS actual wake credential parsing/PS4+PS5 dispatch; Rest Mode requires live session and explicit call; no real console contacted')
