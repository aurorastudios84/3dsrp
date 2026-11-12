#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <jpeglib.h>
#include <math.h>
#include "wire.h"
#include "link_state.h"
#include "video_feedback.h"
#include "net_socket.h"
#include "graphics_cache.h"
#include "controls.h"
#include "controller_wire.h"
#include "motion_3ds.h"
#include "usage_relay.h"
#include "usage_platform.h"
u32 __stacksize__=512*1024;
#define AUDIO_WAVES 4
#define AUDIO_BYTES 960
static atomic_bool stopping,connected,receiver_running;
static atomic_uint packets_total,bytes_total,peer_rejected,header_rejected,session_rejected,fragments_total,fragment_bad,greetings,ping_replies,send_errors;
static atomic_int receive_errno,send_errno;
static atomic_uint current_session,received,shown,decode_errors,replaced,pcm_received,pcm_dropped,underruns,decode_us,pack_us,rtt_ms;
static atomic_uint decode_attempts,graphics_busy,stale_decodes,feedback_missing;
static LightLock picture_lock=1;
static uint8_t pending_jpeg[RELAY_JPEG_MAX],decode_jpeg[RELAY_JPEG_MAX];
static unsigned pending_bytes,pending_seq,pending_session,pending_width,pending_height;
static unsigned image_width=400,image_height=225;
static u64 pending_tick;
static uint16_t rgb[400*225];
static char phone_diagnostics[RELAY_CHUNK+1];
static RelayAssembly assembly;
static struct sockaddr_in phone;
static int sock=-1;
static Thread receiver,decoder;
/* Decoder owns rgb/decode_jpeg. Only completed pictures cross this short lock. */
static LightLock decoded_lock=1;
static uint16_t ready_rgb[400*225],display_rgb[400*225];
static unsigned ready_width,ready_height,ready_session;
static u64 ready_tick;
static bool ready_picture;
static atomic_uint decoded_pictures,decoded_replaced,control_updates;
static ndspWaveBuf waves[AUDIO_WAVES];static void *audio_storage[AUDIO_WAVES];
static bool ndsp_ready;
static unsigned local_age_us,max_age_us,window_max_age_us;
static atomic_uint link_error,remote_release,last_phone_seen,source_state,last_source_seen;
static uint8_t received_packet[RELAY_HEADER+RELAY_CHUNK];
static void send_message(unsigned type,const uint8_t *data,unsigned length,uint32_t stamp){
 uint8_t packet[RELAY_HEADER+128];if(length>128)return;
 relay_header(packet,type,atomic_load(&current_session),type==RELAY_HELLO?RELAY_COMPAT_TAG:0,0,1,length,0,0,stamp);
 if(length)memcpy(packet+RELAY_HEADER,data,length);
 int n=sendto(sock,packet,RELAY_HEADER+length,0,(struct sockaddr*)&phone,sizeof(phone));
 if(n!=(int)(RELAY_HEADER+length)){atomic_store(&send_errno,n<0?errno:EIO);atomic_fetch_add(&send_errors,1);}
}
static void receive_worker(void *unused){
 bool priming=true,played=false;unsigned primed=0;
 atomic_store(&receiver_running,true);
 while(!atomic_load(&stopping)){
  struct sockaddr_in from;socklen_t len=sizeof(from);
  int n=recvfrom(sock,received_packet,sizeof(received_packet),0,(struct sockaddr*)&from,&len);
  if(n<0){if(errno==EINTR)continue;if(errno==EAGAIN||errno==EWOULDBLOCK){svcSleepThread(1000000);continue;}atomic_store(&receive_errno,errno);break;}
  atomic_fetch_add(&packets_total,1);atomic_fetch_add(&bytes_total,(unsigned)n);
  uint8_t *p=received_packet;
  if(from.sin_addr.s_addr!=phone.sin_addr.s_addr||from.sin_port!=phone.sin_port){atomic_fetch_add(&peer_rejected,1);continue;}
  if(!relay_valid(p,(size_t)n)){atomic_fetch_add(&header_rejected,1);continue;}
  unsigned type=p[4],session=r32(p+8);
  if(type==RELAY_ERROR){atomic_store(&remote_release,r32(p+28));atomic_store(&link_error,1);atomic_store(&connected,false);continue;}
  if(type==RELAY_HELLO&&n==RELAY_HEADER&&session){
   atomic_store(&remote_release,r32(p+28));
   if(!relay_hello_compatible(p,n)){atomic_store(&link_error,r32(p+12)!=RELAY_COMPAT_TAG?1:2);atomic_store(&connected,false);continue;}
   atomic_store(&link_error,0);atomic_store(&last_phone_seen,(uint32_t)osGetTime());
   atomic_fetch_add(&greetings,1);
   if(session!=atomic_load(&current_session)){
    atomic_store(&source_state,RELAY_SOURCE_UNKNOWN);atomic_store(&last_source_seen,0);
    memset(&assembly,0,sizeof(assembly));
    if(ndsp_ready){ndspChnWaveBufClear(0);ndspChnSetPaused(0,true);for(unsigned i=0;i<AUDIO_WAVES;i++)waves[i].status=NDSP_WBUF_FREE;}
    priming=true;played=false;primed=0;atomic_store(&current_session,session);
   }
   atomic_store(&connected,true);continue;
  }
  if(!atomic_load(&connected)||!session||session!=atomic_load(&current_session)){atomic_fetch_add(&session_rejected,1);continue;}
  if(type==RELAY_USAGE_ACK){usage_relay_receive(p,(size_t)n);continue;}
  if(type==RELAY_LINK){
   int state=relay_source_packet(p,(size_t)n);if(state<0)continue;
   atomic_store(&source_state,(unsigned)state);atomic_store(&last_source_seen,(uint32_t)osGetTime());atomic_store(&last_phone_seen,(uint32_t)osGetTime());
   if(state==RELAY_SOURCE_STOPPED){atomic_store(&connected,false);atomic_store(&link_error,3);}
   continue;
  }
  if(type==RELAY_PONG&&n==RELAY_HEADER){atomic_store(&last_phone_seen,(uint32_t)osGetTime());atomic_fetch_add(&ping_replies,1);unsigned ms=(uint32_t)osGetTime()-r32(p+28);if(ms<5000)atomic_store(&rtt_ms,ms);continue;}
  if(type==8&&r16(p+16)==0&&r16(p+18)==1&&r32(p+20)==(unsigned)n-RELAY_HEADER){
   unsigned size=(unsigned)n-RELAY_HEADER;LightLock_Lock(&picture_lock);memcpy(phone_diagnostics,p+RELAY_HEADER,size);phone_diagnostics[size]=0;LightLock_Unlock(&picture_lock);continue;
  }
  if(type==RELAY_JPEG){atomic_fetch_add(&fragments_total,1);unsigned old_missing=assembly.abandoned+assembly.missing_sequences;int complete=relay_fragment(&assembly,p,(size_t)n);atomic_fetch_add(&feedback_missing,assembly.abandoned+assembly.missing_sequences-old_missing);if(complete<0)atomic_fetch_add(&fragment_bad,1);if(complete==1){
    LightLock_Lock(&picture_lock);if(pending_bytes)atomic_fetch_add(&replaced,1);
    memcpy(pending_jpeg,assembly.data,assembly.total);pending_bytes=assembly.total;pending_seq=assembly.seq;pending_width=assembly.width;pending_height=assembly.height;pending_session=session;pending_tick=svcGetSystemTick();
    LightLock_Unlock(&picture_lock);atomic_fetch_add(&received,1);
   }continue;
  }
  if(type==RELAY_PCM&&ndsp_ready&&r16(p+16)==0&&r16(p+18)==1&&r16(p+24)==12000&&r16(p+26)==1){
   unsigned bytes=r32(p+20);if(!bytes||bytes>AUDIO_BYTES||(bytes&1)||n!=(int)(RELAY_HEADER+bytes))continue;
   atomic_fetch_add(&pcm_received,1);unsigned free_count=0;int slot=-1;
   for(unsigned i=0;i<AUDIO_WAVES;i++)if(waves[i].status==NDSP_WBUF_FREE||waves[i].status==NDSP_WBUF_DONE){free_count++;if(slot<0)slot=(int)i;}
   if(free_count==AUDIO_WAVES&&played&&!priming){atomic_fetch_add(&underruns,1);ndspChnSetPaused(0,true);priming=true;primed=0;}
   if(slot<0){atomic_fetch_add(&pcm_dropped,1);continue;}
   memcpy(audio_storage[slot],p+RELAY_HEADER,bytes);waves[slot].nsamples=bytes/2;
   if(R_FAILED(DSP_FlushDataCache(audio_storage[slot],bytes))){atomic_fetch_add(&pcm_dropped,1);continue;}
   ndspChnWaveBufAdd(0,&waves[slot]);primed+=bytes/2;
   if(priming&&primed>=240){ndspChnSetPaused(0,false);priming=false;played=true;}
  }
 }
 atomic_store(&receiver_running,false);
}
struct JpegError {struct jpeg_error_mgr base;jmp_buf jump;};
static void jpeg_fail(j_common_ptr info){struct JpegError *err=(struct JpegError*)info->err;longjmp(err->jump,1);}
static void jpeg_quiet(j_common_ptr unused){(void)unused;}
static bool decode_picture(const uint8_t *data,unsigned bytes,unsigned width,unsigned height){
 if(!relay_image_size(width,height))return false;
 struct jpeg_decompress_struct info={0};struct JpegError error;
 info.err=jpeg_std_error(&error.base);error.base.error_exit=jpeg_fail;error.base.output_message=jpeg_quiet;
 if(setjmp(error.jump)){jpeg_destroy_decompress(&info);return false;}
 jpeg_create_decompress(&info);jpeg_mem_src(&info,data,bytes);
 if(jpeg_read_header(&info,TRUE)!=JPEG_HEADER_OK||info.image_width!=width||info.image_height!=height||info.progressive_mode){jpeg_destroy_decompress(&info);return false;}
 info.out_color_space=JCS_RGB565;info.dct_method=JDCT_IFAST;info.do_fancy_upsampling=FALSE;
 if(!jpeg_start_decompress(&info)||info.output_width!=width||info.output_height!=height){jpeg_destroy_decompress(&info);return false;}
 while(info.output_scanline<info.output_height){JSAMPROW row=(JSAMPROW)(rgb+info.output_scanline*width);if(jpeg_read_scanlines(&info,&row,1)!=1){jpeg_destroy_decompress(&info);return false;}}
 bool ok=jpeg_finish_decompress(&info);jpeg_destroy_decompress(&info);return ok;
}
static unsigned spread(unsigned v){return (v&1)|((v&2)<<1)|((v&4)<<2);}
static unsigned micros(u64 start){return (unsigned)((svcGetSystemTick()-start)*1000000u/SYSCLOCK_ARM11);}
static void decode_worker(void *unused){
 while(!atomic_load(&stopping)){
  unsigned size=0,session=0,width=0,height=0;u64 tick=0;
  LightLock_Lock(&picture_lock);
  if(pending_bytes){size=pending_bytes;width=pending_width;height=pending_height;session=pending_session;tick=pending_tick;memcpy(decode_jpeg,pending_jpeg,size);pending_bytes=0;}
  LightLock_Unlock(&picture_lock);
  if(!size){svcSleepThread(1000000);continue;}
  if(atomic_load(&link_error))continue;
  atomic_fetch_add(&decode_attempts,1);u64 before=svcGetSystemTick();
  bool ok=decode_picture(decode_jpeg,size,width,height);
  atomic_fetch_add(&decode_us,micros(before));
  if(!ok){atomic_fetch_add(&decode_errors,1);continue;}
  if(session!=atomic_load(&current_session)){atomic_fetch_add(&stale_decodes,1);continue;}
  if(atomic_load(&stopping))break;
  LightLock_Lock(&decoded_lock);
  if(ready_picture)atomic_fetch_add(&decoded_replaced,1);
  memcpy(ready_rgb,rgb,width*height*sizeof(*rgb));
  ready_width=width;ready_height=height;ready_session=session;ready_tick=tick;ready_picture=true;
  LightLock_Unlock(&decoded_lock);
  atomic_fetch_add(&decoded_pictures,1);
 }
}
/* Never wait for a decode here: claim only its latest completed output. */
static bool claim_picture(unsigned *width,unsigned *height,unsigned *session,u64 *tick){
 bool available=false;
 LightLock_Lock(&decoded_lock);
 if(ready_picture){
  *width=ready_width;*height=ready_height;*session=ready_session;*tick=ready_tick;
  memcpy(display_rgb,ready_rgb,ready_width*ready_height*sizeof(*display_rgb));
  ready_picture=false;available=true;
 }
 LightLock_Unlock(&decoded_lock);
 return available;
}
static bool get_phone(char *text){
 FILE *f=fopen("sdmc:/3ds/3dsrp-relay/phone.txt","rb");if(f){if(!fgets(text,32,f))text[0]=0;fclose(f);text[strcspn(text,"\r\n")]=0;}
 SwkbdState keyboard;swkbdInit(&keyboard,SWKBD_TYPE_NORMAL,2,15);swkbdSetHintText(&keyboard,"Android phone IPv4 address");swkbdSetInitialText(&keyboard,text);swkbdSetValidation(&keyboard,SWKBD_NOTEMPTY_NOTBLANK,0,0);
 return swkbdInputText(&keyboard,text,32)==SWKBD_BUTTON_RIGHT;
}
static float diagnostic_rate(const char *text,const char *key){
 char pattern[48];snprintf(pattern,sizeof(pattern),"\"%s\":",key);const char *p=strstr(text,pattern);if(!p)return -1;
 float f=strtof(p+strlen(pattern),NULL);return isfinite(f)&&f>=0&&f<10000?f:-1;
}
static void quoted_json(FILE *f,const char *text){
 fputc('"',f);for(const unsigned char *p=(const unsigned char*)text;*p;p++){if(*p=='"'||*p=='\\')fputc('\\',f);if(*p<32)fprintf(f,"\\u%04x",*p);else fputc(*p,f);}fputc('"',f);
}
static void save_socket_setup(const RelaySocketSetup *s,bool success){
 /* Startup only, before any receiving/decoding workers exist. */
 FILE *f=fopen("sdmc:/3ds/3dsrp-relay/startup.json","wb");if(!f)return;
 fprintf(f,"{\"version\":8,\"socket_ready\":%s,\"stage\":\"%s\",\"errno\":%d,\"automatic_bind_errno\":%d,\"port_query_errno\":%d,\"receive_buffer_errno\":%d,\"local_port\":%u}\n",success?"true":"false",s->step,s->error,s->automatic_bind_error,s->port_query_error,s->receive_buffer_error,(unsigned)s->local_port);fclose(f);
}
static void usage_apt_hook(APT_HookType hook,void *unused){
 if(hook==APTHOOK_ONSUSPEND||hook==APTHOOK_ONSLEEP)usage_relay_suspend();
}
int main(void){
 bool usage_ready=false;aptHookCookie usage_hook={0};
 RelayControls control_ui;relay_controls_init(&control_ui);
 gfxInit(GSP_BGR8_OES,GSP_RGB565_OES,false);relay_controls_set_console(consoleInit(GFX_BOTTOM,NULL));relay_controls_draw(&control_ui);relay_controls_present();
 GSPGPU_FlushDataCache(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL),320*240*2);
 RelaySocketSetup socket_setup={0};
 uint16_t local_port=0;
 char host[32]="";void *soc_memory=NULL;C3D_RenderTarget *target=NULL;C3D_Tex texture={0};bool c3d=false,c2d=false,texture_ready=false,soc_ready=false;u64 started=osGetTime();const uint32_t run_id=(uint32_t)started;
 if(!get_phone(host))goto cleanup;
 phone.sin_family=AF_INET;phone.sin_port=htons(RELAY_PORT);
 if(inet_pton(AF_INET,host,&phone.sin_addr)!=1){printf("Invalid IPv4 address.\n");goto error;}
 mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/3dsrp-relay",0777);FILE *config=fopen("sdmc:/3ds/3dsrp-relay/phone.txt","wb");if(config){fprintf(config,"%s\n",host);fclose(config);}
 usage_relay_init(run_id,send_message);usage_ready=true;aptHook(&usage_hook,usage_apt_hook,NULL);
 control_ui.usage_allowed=usage_relay_allowed();if(usage_relay_unasked()){control_ui.page=4;control_ui.usage_selection=1;control_ui.revision++;}
 soc_memory=memalign(4096,0x100000);if(!soc_memory||R_FAILED(socInit(soc_memory,0x100000))){printf("Network initialization failed.\n");goto error;}soc_ready=true;
 sock=relay_open_socket(&socket_setup);save_socket_setup(&socket_setup,sock>=0);if(sock<0){printf("Socket %s failed: %d\nAutomatic bind error: %d\n",socket_setup.step,socket_setup.error,socket_setup.automatic_bind_error);goto error;}local_port=socket_setup.local_port;
 c3d=C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);if(!c3d)goto error;c2d=beta_graphics_init();if(!c2d)goto error;
 C2D_Prepare();target=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);if(!target)goto error;
 texture_ready=C3D_TexInit(&texture,512,256,GPU_RGB565);if(!texture_ready)goto error;
 memset(texture.data,0,512*256*2);C3D_TexFlush(&texture);C3D_TexSetFilter(&texture,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
 if(R_FAILED(ndspInit())){printf("Sound initialization failed.\n");goto error;}ndsp_ready=true;
 ndspChnReset(0);ndspChnSetFormat(0,NDSP_FORMAT_MONO_PCM16);ndspChnSetRate(0,12000);ndspChnSetInterp(0,NDSP_INTERP_LINEAR);float mix[12]={1,1};ndspChnSetMix(0,mix);ndspChnSetPaused(0,true);
 for(unsigned i=0;i<AUDIO_WAVES;i++){audio_storage[i]=linearAlloc(AUDIO_BYTES);if(!audio_storage[i])goto error;waves[i].data_vaddr=audio_storage[i];}
 receiver=threadCreate(receive_worker,NULL,64*1024,0x2f,0,false);if(!receiver)goto error;
 /* Lower priority than the UI/input and receiver on original 3DS core 0. */
 decoder=threadCreate(decode_worker,NULL,128*1024,0x31,0,false);if(!decoder){printf("Video worker could not start.\n");goto error;}
 unsigned drawn_revision=~0u,last_connection_view=~0u,last_video_session=0;u64 last_video_shown=0;
 unsigned xo[400],yo[225];for(unsigned x=0;x<400;x++)xo[x]=(x/8)*64+spread(x%8);for(unsigned y=0;y<225;y++)yo[y]=(y/8)*64*64+(spread(y%8)<<1);
 started=osGetTime();u64 last_hello=0,last_status=0,last_feedback=started;RelayVideoCounters previous_feedback={0};unsigned last_shown=0;unsigned controls_seq=0;
 while(aptMainLoop()){
  hidScanInput();unsigned down=hidKeysDown(),held=hidKeysHeld();if(down&KEY_START){if(control_ui.page!=4)break;control_ui.usage_decision=2;}
  u64 now=osGetTime();if(now-last_hello>=1000){send_message(RELAY_HELLO,NULL,0,run_id);send_message(RELAY_PING,NULL,0,(uint32_t)now);
   uint8_t report[12];w32(report,atomic_load(&shown));w32(report+4,atomic_load(&decode_errors));w32(report+8,atomic_load(&fragment_bad));if(atomic_load(&connected))send_message(RELAY_STATUS,report,sizeof(report),0);last_hello=now;}
  if(now-last_feedback>=1000){
   RelayVideoCounters counters={atomic_load(&decoded_pictures),atomic_load(&decode_us),atomic_load(&shown),atomic_load(&pack_us),atomic_load(&received),atomic_load(&feedback_missing),local_age_us,atomic_load(&control_updates)};
   uint8_t feedback[RELAY_FEEDBACK_BYTES];relay_video_feedback(feedback,(uint32_t)(now-last_feedback),&counters,&previous_feedback,window_max_age_us);
   if(atomic_load(&connected))send_message(RELAY_FEEDBACK,feedback,sizeof(feedback),(uint32_t)now);
   last_feedback=now;window_max_age_us=0;
  }
  ChiakiControllerState controller;relay_controls_sample(&control_ui,held,atomic_load(&connected),&controller);
  if(control_ui.usage_decision){
   bool saved=usage_relay_choose(control_ui.usage_decision==1);control_ui.usage_decision=0;
   control_ui.usage_allowed=usage_relay_allowed();control_ui.usage_failed=!saved;
   if(saved){control_ui.page=control_ui.usage_return_page;control_ui.suppress=true;control_ui.extra_down=false;}
   control_ui.revision++;
  }
  uint8_t crop_packet[6];w16(crop_packet,control_ui.crop_x);w16(crop_packet+2,control_ui.crop_y);w16(crop_packet+4,control_ui.magnify?control_ui.zoom_q:4);
  if(atomic_load(&connected))send_message(RELAY_CROP,crop_packet,sizeof(crop_packet),0);
  uint8_t controls[RELAY_CONTROL_BYTES];relay_control_encode(controls,&controller);
  if(atomic_load(&connected)){send_message(RELAY_INPUT_FULL,controls,sizeof(controls),++controls_seq);atomic_fetch_add(&control_updates,1);}
  if(atomic_load(&connected)&&relay_link_expired((uint32_t)now,atomic_load(&last_phone_seen))){atomic_store(&connected,false);atomic_store(&link_error,3);}
  unsigned problem=atomic_load(&link_error);
  if(drawn_revision!=control_ui.revision){if(control_ui.page!=3)relay_controls_draw(&control_ui);else last_status=0;drawn_revision=control_ui.revision;}
  relay_controls_animate(&control_ui,(uint32_t)now);
  unsigned session=0,width=0,height=0;u64 tick=0;
  bool new_picture=claim_picture(&width,&height,&session,&tick);
  bool fresh_picture=new_picture&&session==atomic_load(&current_session)&&micros(tick)<3000000u;
  bool had_video=last_video_shown&&last_video_session==atomic_load(&current_session);
  bool recent_video=fresh_picture||(had_video&&now-last_video_shown<3000);
  unsigned view=relay_connection_view(atomic_load(&connected),problem,atomic_load(&source_state),
   atomic_load(&last_source_seen)&&!relay_link_expired((uint32_t)now,atomic_load(&last_source_seen)),had_video,recent_video);
  bool panel=view!=RELAY_VIEW_LIVE;
  if((panel&&view!=last_connection_view)||(!panel&&fresh_picture)){
   if(panel){relay_connection_draw(display_rgb,view,atomic_load(&source_state));width=400;height=225;}
   if(!C3D_FrameBegin(0))atomic_fetch_add(&graphics_busy,1);
   else{
    if(!panel){image_width=width;image_height=height;}
    u64 before=svcGetSystemTick();uint16_t *pixels=texture.data;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)pixels[yo[y]+xo[x]]=display_rgb[y*width+x];
    C3D_TexFlush(&texture);if(!panel)atomic_fetch_add(&pack_us,micros(before));
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));C2D_SceneBegin(target);
    Tex3DS_SubTexture sub={.width=width,.height=height,.left=0,.top=1,.right=(float)width/512,.bottom=1-(float)height/256};C2D_Image image={&texture,&sub};
    C2D_DrawImageAt(image,0,7,0,NULL,400.0f/width,225.0f/height);beta_graphics_frame_end();last_connection_view=view;
    if(!panel){
     if(atomic_load(&source_state)==RELAY_SOURCE_ACTIVE)usage_relay_frame(session,usage_platform_millis());
     last_video_shown=now;last_video_session=session;atomic_fetch_add(&shown,1);unsigned age=micros(tick);local_age_us+=age;
     if(age>max_age_us)max_age_us=age;
     if(age>window_max_age_us)window_max_age_us=age;
    }
   }
  }

  usage_relay_tick(atomic_load(&connected),view==RELAY_VIEW_LIVE&&atomic_load(&source_state)==RELAY_SOURCE_ACTIVE,usage_platform_millis());
  if(now-last_status>=1000){unsigned frames=atomic_load(&shown),interval=(unsigned)(now-last_status),fps10=interval?(frames-last_shown)*10000/interval:0;
   char diagnostics[RELAY_CHUNK+1];LightLock_Lock(&picture_lock);memcpy(diagnostics,phone_diagnostics,sizeof(diagnostics));LightLock_Unlock(&picture_lock);
   if(control_ui.page==3)printf("\x1b[2J\x1b[H3DSRP diagnostics - release %u\nPhone: %s\n%s / local port %u\nReceiver: %s / socket error %d\nPackets: %u / %u KiB\nHello: %u / ping replies: %u\nJPEG parts: %u / bad: %u\nPictures: %u received / %u shown\nShown: %u.%u fps\nDecode errors: %u\nSound: %u / dropped %u\nRejected peer/header/session: %u/%u/%u\nSend errors: %u / last: %d\nNetwork round trip: %u ms\nPhone input/decode/send FPS:\n%.1f / %.1f / %.1f\n\nTap: back  SELECT: controls  START: stop\n",RELAY_RELEASE,host,atomic_load(&connected)?"Connected":"Waiting",(unsigned)local_port,atomic_load(&receiver_running)?"running":"STOPPED",atomic_load(&receive_errno),atomic_load(&packets_total),atomic_load(&bytes_total)/1024,atomic_load(&greetings),atomic_load(&ping_replies),atomic_load(&fragments_total),atomic_load(&fragment_bad),atomic_load(&received),frames,fps10/10,fps10%10,atomic_load(&decode_errors),atomic_load(&pcm_received),atomic_load(&pcm_dropped),atomic_load(&peer_rejected),atomic_load(&header_rejected),atomic_load(&session_rejected),atomic_load(&send_errors),atomic_load(&send_errno),atomic_load(&rtt_ms),diagnostic_rate(diagnostics,"input_fps"),diagnostic_rate(diagnostics,"decoded_fps"),diagnostic_rate(diagnostics,"sent_fps"));
   GSPGPU_FlushDataCache(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL),320*240*2);
   last_status=now;last_shown=frames;
  }
  gspWaitForVBlank();relay_controls_present();
 }
 goto cleanup;
error:
 printf("Could not start relay. START to exit.\n");GSPGPU_FlushDataCache(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL),320*240*2);while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gspWaitForVBlank();}
cleanup:
 if(usage_ready){aptUnhook(&usage_hook);usage_relay_shutdown(sock>=0&&atomic_load(&connected));}
 motion_3ds_poll(false);relay_controls_save(&control_ui);
 if(sock>=0){send_message(RELAY_BYE,NULL,0,0);}
 atomic_store(&stopping,true);if(receiver){threadJoin(receiver,U64_MAX);threadFree(receiver);}
 if(decoder){threadJoin(decoder,U64_MAX);threadFree(decoder);}
 if(ndsp_ready){ndspChnWaveBufClear(0);ndspChnReset(0);ndspExit();}for(unsigned i=0;i<AUDIO_WAVES;i++)linearFree(audio_storage[i]);
 if(receiver){FILE *f=fopen("sdmc:/3ds/3dsrp-relay/results.json","wb");if(f){fprintf(f,"{\"version\":8,\"width\":%u,\"height\":%u,\"codec\":\"jpeg\",\"color\":true,\"audio_hz\":12000,\"audio_channels\":1,\"duration_ms\":%llu,\"received\":%u,\"shown\":%u,\"replaced\":%u,\"decode_errors\":%u,\"decode_us\":%u,\"texture_pack_us\":%u,\"audio_packets\":%u,\"audio_dropped\":%u,\"audio_underruns\":%u,\"network_rtt_ms\":%u,\"local_age_us\":%u,\"max_local_age_us\":%u",image_width,image_height,(unsigned long long)(osGetTime()-started),atomic_load(&received),atomic_load(&shown),atomic_load(&replaced),atomic_load(&decode_errors),atomic_load(&decode_us),atomic_load(&pack_us),atomic_load(&pcm_received),atomic_load(&pcm_dropped),atomic_load(&underruns),atomic_load(&rtt_ms),local_age_us,max_age_us);
 fprintf(f,",\"decode_attempts\":%u,\"graphics_busy\":%u,\"stale_decodes\":%u",atomic_load(&decode_attempts),atomic_load(&graphics_busy),atomic_load(&stale_decodes));
 fprintf(f,",\"async_decode\":true,\"decoded_pictures\":%u,\"decoded_replaced\":%u,\"control_updates\":%u",atomic_load(&decoded_pictures),atomic_load(&decoded_replaced),atomic_load(&control_updates));
 fprintf(f,",\"app_release\":%u",RELAY_RELEASE);
 fprintf(f,",\"local_port\":%u,\"packets\":%u,\"packet_bytes\":%u,\"greetings\":%u,\"ping_replies\":%u,\"jpeg_fragments\":%u,\"bad_fragments\":%u,\"peer_rejected\":%u,\"header_rejected\":%u,\"session_rejected\":%u,\"receive_errno\":%d,\"send_errors\":%u,\"send_errno\":%d,\"automatic_bind_errno\":%d,\"port_query_errno\":%d,\"receive_buffer_errno\":%d,\"incomplete_pictures\":%u,\"missing_fragments\":%u,\"missing_sequences\":%u,\"phone_diagnostics\":",(unsigned)local_port,atomic_load(&packets_total),atomic_load(&bytes_total),atomic_load(&greetings),atomic_load(&ping_replies),atomic_load(&fragments_total),atomic_load(&fragment_bad),atomic_load(&peer_rejected),atomic_load(&header_rejected),atomic_load(&session_rejected),atomic_load(&receive_errno),atomic_load(&send_errors),atomic_load(&send_errno),socket_setup.automatic_bind_error,socket_setup.port_query_error,socket_setup.receive_buffer_error,assembly.abandoned,assembly.missing_fragments,assembly.missing_sequences);quoted_json(f,phone_diagnostics);fputs("}\n",f);fclose(f);}}
 if(sock>=0)close(sock);
 if(texture_ready)C3D_TexDelete(&texture);
 if(target)C3D_RenderTargetDelete(target);
 if(c2d)C2D_Fini();
 if(c3d)C3D_Fini();
 if(soc_ready)socExit();
 free(soc_memory);gfxExit();return 0;
}
