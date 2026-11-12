#include "../../../../../common/jni_binding.hpp"
#include <thread>
#include <chrono>
#include "../../../../../common/scale.h"
// Local Remote Play bridge. Credentials stay in Android app-private storage.
#include <jni.h>
#include <chiaki/session.h>
#include <chiaki/discovery.h>
#include "stream_profile.h"
#include "controller_wire.h"
#include "ctrl_failure.hpp"
#include <chiaki/regist.h>
#include <chiaki/log.h>
#include <mutex>
#include <atomic>
#include <string>
#include <cstdio>
#include <cstring>
#include <unistd.h>
static JavaVM *vm;
static RelayCtrlFailure ctrl_failure;
static void connection_log(ChiakiLogLevel level,const char *message,void*){if(level==CHIAKI_LOG_ERROR)ctrl_failure.observe(message);}
static std::atomic<uint64_t> connection[3];
static std::atomic<bool> console_connected{false};
extern "C" JNIEXPORT jlongArray JNICALL Java_com_aurora_relay_RelayEngine_nativeConnection(JNIEnv *e,jobject){jlong v[3];for(unsigned i=0;i<3;i++)v[i]=connection[i].load();jlongArray a=e->NewLongArray(3);if(a)e->SetLongArrayRegion(a,0,3,v);return a;}
static std::atomic<uint64_t> video_callbacks,audio_callbacks,callback_ns,callback_max_ns;
struct CallbackCost {
 std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
 ~CallbackCost(){uint64_t n=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();callback_ns.fetch_add(n,std::memory_order_relaxed);uint64_t old=callback_max_ns.load();while(n>old&&!callback_max_ns.compare_exchange_weak(old,n)){};}
};
static struct {
 std::mutex lock;
 std::atomic<bool> stopping{false};
 std::atomic<uint64_t> generation{0};
 jobject owner=nullptr;
 jmethodID video=nullptr,audio=nullptr,header=nullptr,status=nullptr,connected=nullptr,quit=nullptr;
 ChiakiSession session{};bool initialized=false,started=false;
 ChiakiLog log{};
} g;
struct Env {JNIEnv *e;Env():e(relay_jni_env(vm)){};};
static void status(const char *s) {
 if(!g.owner)return;Env env;if(!env.e)return;jstring t=env.e->NewStringUTF(s);
 if(t){env.e->CallVoidMethod(g.owner,g.status,t);env.e->DeleteLocalRef(t);}
 if(env.e->ExceptionCheck())env.e->ExceptionClear();
}
// Callbacks report outcomes only. Java's connection worker joins/finalizes attempts.
static void event_cb(ChiakiEvent *event,void*) {
 if(g.stopping||!g.owner)return;
 if(event->type==CHIAKI_EVENT_CONNECTED){
  console_connected=true;connection[0]=g.session.mtu_in;connection[1]=g.session.mtu_out;connection[2]=g.session.rtt_us;
  Env env;if(env.e){env.e->CallVoidMethod(g.owner,g.connected);if(env.e->ExceptionCheck())env.e->ExceptionClear();}
 }
 if(event->type==CHIAKI_EVENT_QUIT){
  console_connected=false;std::string reason=std::string("Disconnected: ")+chiaki_quit_reason_string(event->quit.reason);
  std::string detail=ctrl_failure.detail();if(!detail.empty())reason+=" ("+detail+")";
  uint32_t application=ctrl_failure.application();
  bool retryable=false;
  if(!application){
   retryable=event->quit.reason==CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED||
    event->quit.reason==CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN||
    ((event->quit.reason==CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED||event->quit.reason==CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED)&&ctrl_failure.retryable());
  }
  if(application==CHIAKI_RP_APPLICATION_REASON_CRASH||event->quit.reason==CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH)
   reason+=". Remote Play on the console reported a crash. Restart the PS5 and try again.";
  else if(application==CHIAKI_RP_APPLICATION_REASON_IN_USE||event->quit.reason==CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE)
   reason+=". Disconnect any other Remote Play session and try again.";
  else if(application==CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED||application==CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID)
   reason+=". Sign in to the intended console user and pair again locally with a fresh Link Device PIN.";
  else if(application==CHIAKI_RP_APPLICATION_REASON_RP_VERSION||event->quit.reason==CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH)
   reason+=". Check for companion and console updates.";
  else if(ctrl_failure.http()==403)
   reason+=". If this persists after restarting the console, sign in to the paired user and pair again locally.";
  Env env;if(env.e){jstring text=env.e->NewStringUTF(reason.c_str());if(text){env.e->CallVoidMethod(g.owner,g.quit,text,(jboolean)retryable);env.e->DeleteLocalRef(text);}if(env.e->ExceptionCheck())env.e->ExceptionClear();}
 }
 if(event->type==CHIAKI_EVENT_LOGIN_PIN_REQUEST)status("Console sign-in requires a login passcode. Sign in on the console before connecting.");
}
static void display_cb(void*,bool unavailable){if(unavailable)status("Console temporarily cannot share this screen.");}
static bool video_cb(uint8_t *data,size_t size,int32_t lost,bool,void*) {
 if(g.stopping||!g.owner||size>2*1024*1024)return false;
 CallbackCost timer;video_callbacks.fetch_add(1,std::memory_order_relaxed);
 Env env;if(!env.e)return false;jbyteArray b=env.e->NewByteArray((jsize)size);if(!b){if(env.e->ExceptionCheck())env.e->ExceptionClear();return false;}
 env.e->SetByteArrayRegion(b,0,(jsize)size,(jbyte*)data);
 bool accepted=env.e->CallBooleanMethod(g.owner,g.video,b,(jint)lost);
 env.e->DeleteLocalRef(b);
 if(env.e->ExceptionCheck()){env.e->ExceptionClear();accepted=false;}
 if(!accepted)chiaki_session_request_idr(&g.session);
 return accepted;
}
static void audio_header(ChiakiAudioHeader *h,void*) {
 if(!g.owner||g.stopping)return;Env env;if(!env.e)return;
 env.e->CallVoidMethod(g.owner,g.header,(jint)h->rate,(jint)h->channels,(jint)h->frame_size);
 if(env.e->ExceptionCheck())env.e->ExceptionClear();
}
static void audio_cb(uint8_t *data,size_t size,void*) {
 if(!g.owner||g.stopping||!data||!size||size>4096)return;
 CallbackCost timer;audio_callbacks.fetch_add(1,std::memory_order_relaxed);
 Env env;if(!env.e)return;jbyteArray b=env.e->NewByteArray((jsize)size);if(!b){if(env.e->ExceptionCheck())env.e->ExceptionClear();return;}
 env.e->SetByteArrayRegion(b,0,(jsize)size,(jbyte*)data);
 env.e->CallVoidMethod(g.owner,g.audio,b);env.e->DeleteLocalRef(b);
 if(env.e->ExceptionCheck())env.e->ExceptionClear();
}
static void stop_locked(JNIEnv *e) {
 if(g.started){chiaki_session_stop(&g.session);chiaki_session_join(&g.session);g.started=false;}
 if(g.initialized){chiaki_session_fini(&g.session);g.initialized=false;}
 if(g.owner){e->DeleteGlobalRef(g.owner);g.owner=nullptr;}
}
struct Registration {std::atomic<bool> done{false};bool success=false;ChiakiRegisteredHost host{};};
static void regist_cb(ChiakiRegistEvent *ev,void *p) {
 auto *r=(Registration*)p;
 if(ev->type==CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS){r->host=*ev->registered_host;r->success=true;}
 r->done.store(true,std::memory_order_release);
}
struct Pair {uint32_t magic=0x52505231;char address[64]{};uint8_t account[8]{};ChiakiRegisteredHost host{};};
extern "C" JNIEXPORT jboolean JNICALL Java_com_aurora_relay_RelayEngine_nativeHasPair(JNIEnv *e,jclass,jstring address,jbyteArray account,jstring path,jboolean ps5){
 if(!address||!path||!account||e->GetArrayLength(account)!=8)return JNI_FALSE;
 const char *host=e->GetStringUTFChars(address,nullptr);if(!host)return JNI_FALSE;
 const char *file=e->GetStringUTFChars(path,nullptr);if(!file){e->ReleaseStringUTFChars(address,host);return JNI_FALSE;}
 uint8_t aid[8];e->GetByteArrayRegion(account,0,8,(jbyte*)aid);
 Pair pair;FILE *f=fopen(file,"rb");bool valid=false;
 if(f){valid=fread(&pair,1,sizeof(pair),f)==sizeof(pair)&&pair.magic==0x52505231&&!memcmp(pair.account,aid,8)&&chiaki_target_is_ps5(pair.host.target)==(bool)ps5;fclose(f);}
 e->ReleaseStringUTFChars(path,file);e->ReleaseStringUTFChars(address,host);return valid?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jstring JNICALL Java_com_aurora_relay_RelayEngine_nativeWake(JNIEnv *e,jclass,jstring address,jbyteArray account,jstring path,jboolean ps5){
 if(!Java_com_aurora_relay_RelayEngine_nativeHasPair(e,nullptr,address,account,path,ps5))return e->NewStringUTF("Pair this console locally before using Wake.");
 const char *file=e->GetStringUTFChars(path,nullptr);if(!file)return nullptr;
 Pair pair;FILE *f=fopen(file,"rb");bool ok=f&&fread(&pair,1,sizeof(pair),f)==sizeof(pair);if(f)fclose(f);e->ReleaseStringUTFChars(path,file);
 if(!ok)return e->NewStringUTF("Could not read saved pairing.");
 char credential[9]={0};size_t length=0;
 while(length<8&&pair.host.rp_regist_key[length]){unsigned char c=pair.host.rp_regist_key[length];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return e->NewStringUTF("Saved wake credential is invalid.");credential[length++]=c;}
 if(!length)return e->NewStringUTF("Saved wake credential is empty.");
 for(size_t i=length;i<16;i++)if(pair.host.rp_regist_key[i])return e->NewStringUTF("Saved wake credential is invalid.");
 const char *host=e->GetStringUTFChars(address,nullptr);if(!host)return nullptr;
 ChiakiLog log;chiaki_log_init(&log,0,nullptr,nullptr);chiaki_lib_init();
 ChiakiErrorCode error=chiaki_discovery_wakeup(&log,nullptr,host,strtoull(credential,nullptr,16),ps5);e->ReleaseStringUTFChars(address,host);
 return error==CHIAKI_ERR_SUCCESS?nullptr:e->NewStringUTF("Could not send wake request. Check the address and network.");
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_aurora_relay_RelayEngine_nativeRest(JNIEnv*,jobject){
 std::lock_guard<std::mutex> guard(g.lock);
 if(!g.started||g.stopping||!console_connected)return JNI_FALSE;
 if(chiaki_session_goto_bed(&g.session)!=CHIAKI_ERR_SUCCESS)return JNI_FALSE;
 /* Let the control thread send the request before normal session shutdown.
  * The console normally closes its connection when entering Rest Mode. */
 for(unsigned i=0;i<200&&console_connected&&!g.stopping;i++)usleep(10000);
 return JNI_TRUE;
}
extern "C" JNIEXPORT jstring JNICALL Java_com_aurora_relay_RelayEngine_nativeStart(JNIEnv *e,jobject self,jstring address,jbyteArray account,jstring pin,jstring path,jlong generation,jint height,jint bitrate,jint fps,jboolean ps5) {
 ChiakiConnectVideoProfile requested{};if(!relay_stream_profile(&requested,height,bitrate,fps))return e->NewStringUTF("Invalid console resolution, bitrate or frame rate.");
 std::lock_guard<std::mutex> guard(g.lock);
 if((uint64_t)generation!=g.generation.load())return e->NewStringUTF("Canceled.");
 stop_locked(e);if((uint64_t)generation!=g.generation.load())return e->NewStringUTF("Canceled.");console_connected=false;g.stopping=false;ctrl_failure.reset();
 if(e->GetArrayLength(account)!=8)return e->NewStringUTF("Account ID must decode to 8 bytes.");
 auto str=[&](jstring j){const char *p=e->GetStringUTFChars(j,nullptr);std::string s(p?p:"");if(p)e->ReleaseStringUTFChars(j,p);return s;};
 std::string ip=str(address),code=str(pin),file=str(path);Pair pair;
 if(ip.size()>63)return e->NewStringUTF("Invalid console address.");
 uint8_t aid[8];e->GetByteArrayRegion(account,0,8,(jbyte*)aid);
 g.owner=e->NewGlobalRef(self);jclass c=e->GetObjectClass(self);
 g.video=e->GetMethodID(c,"onVideo","([BI)Z");g.audio=e->GetMethodID(c,"onAudio","([B)V");
 g.header=e->GetMethodID(c,"onAudioHeader","(III)V");
 g.connected=e->GetMethodID(c,"onConsoleConnected","()V");g.quit=e->GetMethodID(c,"onConsoleQuit","(Ljava/lang/String;Z)V");g.status=e->GetMethodID(c,"onStatus","(Ljava/lang/String;)V");e->DeleteLocalRef(c);
 chiaki_log_init(&g.log,CHIAKI_LOG_ERROR,connection_log,nullptr);chiaki_lib_init();
 bool cached=false;FILE *f=fopen(file.c_str(),"rb");
 if(f){cached=fread(&pair,1,sizeof(pair),f)==sizeof(pair)&&pair.magic==0x52505231&&!memcmp(pair.account,aid,8)&&chiaki_target_is_ps5(pair.host.target)==(bool)ps5;fclose(f);}
 if(!code.empty())cached=false;
 if(!cached){
  if(code.size()!=8||code.find_first_not_of("0123456789")!=std::string::npos){stop_locked(e);return e->NewStringUTF("Enter the console's eight-digit Link Device PIN for first registration.");}
  status("Registering with console...");Registration r;ChiakiRegist reg{};ChiakiRegistInfo info{};
  info.target=ps5?CHIAKI_TARGET_PS5_1:CHIAKI_TARGET_PS4_10;info.host=ip.c_str();info.pin=(uint32_t)strtoul(code.c_str(),nullptr,10);memcpy(info.psn_account_id,aid,8);
  ChiakiErrorCode error=chiaki_regist_start(&reg,&g.log,&info,regist_cb,&r);
  if(error!=CHIAKI_ERR_SUCCESS){stop_locked(e);return e->NewStringUTF("Could not start console registration.");}
  for(unsigned i=0;i<3000&&!r.done.load(std::memory_order_acquire)&&!g.stopping;i++)usleep(10000);
  if(!r.done.load(std::memory_order_acquire))chiaki_regist_stop(&reg);
  chiaki_regist_fini(&reg);
  if(!r.success||g.stopping){stop_locked(e);return e->NewStringUTF("Registration failed or canceled. Check IP, Account ID and fresh Link Device PIN.");}
  pair=Pair{};strncpy(pair.address,ip.c_str(),63);memcpy(pair.account,aid,8);pair.host=r.host;
  std::string temporary=file+".tmp";f=fopen(temporary.c_str(),"wb");if(f){bool ok=fwrite(&pair,1,sizeof(pair),f)==sizeof(pair);ok=(fclose(f)==0)&&ok;if(!ok||rename(temporary.c_str(),file.c_str())!=0){remove(temporary.c_str());stop_locked(e);return e->NewStringUTF("Could not save registration.");}}
  else {stop_locked(e);return e->NewStringUTF("Could not save registration.");}
 }
 if(g.stopping){stop_locked(e);return e->NewStringUTF("Canceled.");}
 status("Console paired");
 ChiakiConnectInfo info{};info.ps5=ps5;info.host=ip.c_str();memcpy(info.psn_account_id,aid,8);
 memcpy(info.regist_key,pair.host.rp_regist_key,16);memcpy(info.morning,pair.host.rp_key,16);
 info.video_profile=requested;info.packet_loss_max=.05;info.enable_idr_on_fec_failure=false;
 ChiakiErrorCode error=chiaki_session_init(&g.session,&info,&g.log);
 if(error!=CHIAKI_ERR_SUCCESS){stop_locked(e);return e->NewStringUTF("Console session initialization failed.");}
 g.initialized=true;ChiakiCtrlDisplaySink display{};display.cantdisplay_cb=display_cb;chiaki_session_ctrl_set_display_sink(&g.session,&display);chiaki_session_set_event_cb(&g.session,event_cb,nullptr);chiaki_session_set_video_sample_cb(&g.session,video_cb,nullptr);
 ChiakiAudioSink sink{};sink.header_cb=audio_header;sink.frame_cb=audio_cb;chiaki_session_set_audio_sink(&g.session,&sink);
 if(g.stopping||(uint64_t)generation!=g.generation.load()){stop_locked(e);return e->NewStringUTF("Canceled.");}
 error=chiaki_session_start(&g.session);
 if(error!=CHIAKI_ERR_SUCCESS){stop_locked(e);return e->NewStringUTF("Could not start console session.");}
 g.started=true;return nullptr;
}
extern "C" JNIEXPORT void JNICALL Java_com_aurora_relay_RelayEngine_nativeStopAttempt(JNIEnv *e,jobject,jlong generation) {
 std::lock_guard<std::mutex> guard(g.lock);
 if((uint64_t)generation!=g.generation.load())return;
 g.stopping=true;stop_locked(e);console_connected=false;
}
extern "C" JNIEXPORT void JNICALL Java_com_aurora_relay_RelayEngine_nativeStop(JNIEnv *e,jobject) {
 g.generation.fetch_add(1);g.stopping=true;std::lock_guard<std::mutex> guard(g.lock);stop_locked(e);
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_aurora_relay_RelayEngine_nativeControls(JNIEnv*,jobject,jint buttons,jint lx,jint ly,jint l2,jint r2) {
 std::unique_lock<std::mutex> guard(g.lock,std::try_to_lock);if(!guard.owns_lock()||!g.started||g.stopping)return JNI_FALSE;
 ChiakiControllerState c;chiaki_controller_state_set_idle(&c);c.buttons=(uint32_t)buttons;c.left_x=(int16_t)lx;c.left_y=(int16_t)ly;c.l2_state=(uint8_t)l2;c.r2_state=(uint8_t)r2;
 return chiaki_session_set_controller_state(&g.session,&c)==CHIAKI_ERR_SUCCESS?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_aurora_relay_RelayEngine_nativeFullControls(JNIEnv *e,jobject,jbyteArray packet,jint offset) {
 if(!packet||offset<0||offset>e->GetArrayLength(packet)||e->GetArrayLength(packet)-offset<(int)RELAY_CONTROL_BYTES)return JNI_FALSE;
 uint8_t data[RELAY_CONTROL_BYTES];e->GetByteArrayRegion(packet,offset,RELAY_CONTROL_BYTES,(jbyte*)data);if(e->ExceptionCheck()){e->ExceptionClear();return JNI_FALSE;}
 ChiakiControllerState c;if(!relay_control_decode(data,sizeof(data),&c))return JNI_FALSE;
 std::unique_lock<std::mutex> guard(g.lock,std::try_to_lock);if(!guard.owns_lock()||!g.started||g.stopping)return JNI_FALSE;
 return chiaki_session_set_controller_state(&g.session,&c)==CHIAKI_ERR_SUCCESS?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *v,void*){vm=v;return JNI_VERSION_1_6;}

extern "C" JNIEXPORT jlong JNICALL Java_com_aurora_relay_RelayEngine_nativePrepare(JNIEnv*,jobject){for(auto &c:connection)c=0;video_callbacks=0;audio_callbacks=0;callback_ns=0;callback_max_ns=0;return (jlong)g.generation.fetch_add(1)+1;}

extern "C" JNIEXPORT jboolean JNICALL Java_com_aurora_relay_RelayEngine_nativeScale(JNIEnv *e,jobject,jobject y,jobject u,jobject v,jintArray meta,jbyteArray output,jint width,jint height){
 if(e->GetArrayLength(meta)!=13)return JNI_FALSE;
 jint layout[13];e->GetIntArrayRegion(meta,0,13,layout);
 jobject objects[3]={y,u,v};const uint8_t *planes[3];size_t capacity[3];
 for(unsigned i=0;i<3;i++){
  jlong n=e->GetDirectBufferCapacity(objects[i]);planes[i]=(const uint8_t*)e->GetDirectBufferAddress(objects[i]);if(n<0||!planes[i])return JNI_FALSE;capacity[i]=(size_t)n;
 }
 jsize bytes=e->GetArrayLength(output);jbyte *dest=(jbyte*)e->GetPrimitiveArrayCritical(output,nullptr);if(!dest)return JNI_FALSE;
 int ok=relay_scale_nv21(planes,capacity,layout,(uint8_t*)dest,(size_t)bytes,width,height);
 e->ReleasePrimitiveArrayCritical(output,dest,0);return ok?JNI_TRUE:JNI_FALSE;
}

extern "C" JNIEXPORT jlongArray JNICALL Java_com_aurora_relay_RelayEngine_nativeAttachmentProbe(JNIEnv *e,jobject){
 jlong totals[3]={0,0,256};
 std::thread worker([&](){
  for(unsigned pass=0;pass<2;pass++){
   auto begin=std::chrono::steady_clock::now();
   for(unsigned i=0;i<256;i++){
    if(pass==0){RelayJniBinding binding(vm);if(!binding.env)break;jbyteArray b=binding.env->NewByteArray(480);if(b)binding.env->DeleteLocalRef(b);}
    else {JNIEnv *env=relay_jni_env(vm);if(!env)break;jbyteArray b=env->NewByteArray(480);if(b)env->DeleteLocalRef(b);}
   }
   totals[pass]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
  }
 });worker.join();jlongArray result=e->NewLongArray(3);if(result)e->SetLongArrayRegion(result,0,3,totals);return result;
}

extern "C" JNIEXPORT jlongArray JNICALL Java_com_aurora_relay_RelayEngine_nativeBridgeStats(JNIEnv *e,jobject){
 jlong values[4]={(jlong)video_callbacks.load(),(jlong)audio_callbacks.load(),(jlong)(callback_ns.load()/1000),(jlong)(callback_max_ns.load()/1000)};
 jlongArray result=e->NewLongArray(4);if(result)e->SetLongArrayRegion(result,0,4,values);return result;
}

#include "jpeg_encode.h"
#include <vector>
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_aurora_relay_RelayEngine_nativeJpeg(JNIEnv *e,jobject,jbyteArray input,jint quality,jint width,jint height){
 unsigned nvbytes=relay_nv21_bytes(width,height);
 if(!relay_image_size(width,height)||!input||e->GetArrayLength(input)!=(jsize)nvbytes||quality<1||quality>100)return nullptr;
 std::vector<unsigned char> nv(nvbytes);e->GetByteArrayRegion(input,0,nvbytes,(jbyte*)nv.data());if(e->ExceptionCheck())return nullptr;
 unsigned long size=0;unsigned char *bytes=relay_encode_jpeg(nv.data(),quality,&size,width,height);if(!bytes)return nullptr;
 jbyteArray out=e->NewByteArray((jsize)size);if(out)e->SetByteArrayRegion(out,0,(jsize)size,(jbyte*)bytes);free(bytes);return out;
}
