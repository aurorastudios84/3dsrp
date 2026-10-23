/* Discovery, transactional registration and authenticated Remote Play. */
#include "rp_client.h"
#include <chiaki/discovery.h>
#include <errno.h>
#include <chiaki/log.h>
#include <chiaki/random.h>
#include <chiaki/thread.h>
#include <chiaki/time.h>
#include <chiaki/videoreceiver.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#ifdef __3DS__
#include <3ds.h>
#endif

#ifndef RP_WAKE_TIMEOUT_MS
#define RP_WAKE_TIMEOUT_MS 60000
#endif

#ifdef RP_TEST_LOOPBACK
#define CONSOLE_IP "127.0.0.1"
#else
#define CONSOLE_IP RP_CONSOLE_IP
#endif

#define RP_CLEANUP_WORKER_OBJECT 0xffff2210u

struct RpClient {
    AppSettings stream_settings;
    ChiakiLog log;
    ChiakiRegist registration;
    ChiakiSession session;
    AudioPipeline *audio;
    ChiakiDiscovery discovery;
    ChiakiDiscoveryThread discovery_thread;
    bool discovery_live, registration_live, session_initialized, session_started, audio_started;
    bool have_saved, have_candidate;
    PairRecord saved, candidate;
    char target_address[64], target_nickname[32], target_identity[17];
    bool adding_pair;
    atomic_int state, error;
    atomic_bool verified;
    atomic_int rest_mode_send_result;
    RpVideoCallback video_cb;
    void *video_cb_user;
    ChiakiThread cleanup_thread;
    ChiakiThread startup_thread;
    bool startup_live;
    bool automatic_wake;
    bool cleanup_live;
    uint32_t last_idr_request;
    atomic_bool stopping, cleanup_done, cleanup_failed;
    RpMediaJoinCallback media_join_callback;
    void *media_join_user;
};

void rp_set_media_join_callback(RpClient *client, RpMediaJoinCallback callback, void *user) {
    client->media_join_callback=callback; client->media_join_user=user;
}

static bool digits(const char *s, size_t n) {
    if(!s || strlen(s)!=n) return false;
    for(size_t i=0;i<n;i++) if(s[i]<'0' || s[i]>'9') return false;
    return true;
}

static void fail(RpClient *c, RpError error) {

    atomic_store(&c->error,error); atomic_store(&c->state,RP_FAILED);
}
static RpError error_code(ChiakiErrorCode e) {
    switch(e) {
        case CHIAKI_ERR_TIMEOUT: return RP_ERROR_TIMEOUT;
        case CHIAKI_ERR_NETWORK: return RP_ERROR_NETWORK;
        case CHIAKI_ERR_CONNECTION_REFUSED: return RP_ERROR_NETWORK;
        default: return RP_ERROR_PROTOCOL;
    }
}
/* Session teardown runs after startup has joined. */
static void rp_stop_session(RpClient *c) {

    if(c->discovery_live) {

        ChiakiErrorCode result=chiaki_discovery_thread_stop(&c->discovery_thread);

        if(result!=CHIAKI_ERR_SUCCESS) {
            atomic_store(&c->cleanup_failed,true);  return;
        }

        do { chiaki_discovery_fini(&c->discovery); } while(0); c->discovery_live=false;

    }
    if(c->registration_live) {

        do { chiaki_regist_stop(&c->registration); } while(0);

        do { chiaki_regist_fini(&c->registration); } while(0);

        secret_clear(&c->registration,sizeof(c->registration)); c->registration_live=false;
    }
    if(c->session_started) {

        ChiakiErrorCode result=chiaki_session_stop(&c->session);

        if(result!=CHIAKI_ERR_SUCCESS) {
            atomic_store(&c->cleanup_failed,true);  return;
        }

        /* Let each session owner stop its own workers. A global I/O cancel
         * makes CTRL's notification wait return CANCELED before CTRL's
         * should_stop is set; CTRL then retries that wait without sleeping.
         * Session shutdown reaches chiaki_ctrl_stop after stream cleanup. */

        result=chiaki_session_join(&c->session);

        if(result!=CHIAKI_ERR_SUCCESS) {
            atomic_store(&c->cleanup_failed,true);  return;
        }
        c->session_started=false;
    }
    /* No audio callback can run after the session producer has joined. The
     * output worker alone owns NDSP and its linear buffers. */
    if(c->audio_started) {

        do { audio_pipeline_stop(c->audio); } while(0);

        for(;;) {

            bool joined=audio_pipeline_join(c->audio);

            if(joined) break;
            usleep(2000);
        }

        c->audio_started=false;
    }
    /* Early video cancellation happens on the UI; only this cleanup owner joins
     * it. Retain the session until every media worker has actually returned. */
    if(rp_stopping(c) && c->media_join_callback) {

        for(;;) {

            bool joined=c->media_join_callback(c->media_join_user);

            if(joined) break;
            usleep(2000);
        }

    }
    if(c->session_initialized) {

        do { chiaki_session_fini(&c->session); } while(0); c->session_initialized=false;

    }
    secret_clear(&c->candidate,sizeof(c->candidate)); c->have_candidate=false;
    atomic_store(&c->verified,false);
    atomic_store(&c->state,c->have_saved ? RP_PAIRED:RP_IDLE);

}
void rp_stop(RpClient *c) { rp_stop_session(c); }

static void *cleanup_worker(void *user) {
    RpClient *client=user;

    while(!atomic_load(&client->stopping)) usleep(2000);

    if(client->startup_live) {

        ChiakiErrorCode result=chiaki_thread_join(&client->startup_thread,NULL);

        if(result!=CHIAKI_ERR_SUCCESS) {

            atomic_store(&client->cleanup_failed,true);

            return NULL;
        }
        client->startup_live=false;
    }
    if(client->session_started) {
        /* Timing experiment: the UI has already requested video cancellation.
         * Give the decoder one second before requesting network shutdown, on
         * this background worker only. This is not a decoder-completion fence;
         * all existing joins and resource-retention rules remain necessary. */

#ifdef __3DS__
        svcSleepThread(1000000000LL);
#else
        usleep(1000000);
#endif
        {}
    }

    do { rp_stop_session(client); } while(0);

    if(!atomic_load(&client->cleanup_failed)) {

        do { atomic_store(&client->cleanup_done,true); } while(0);

    } else {

    }
    return NULL;
}
bool rp_request_stop(RpClient *client) {

    do { atomic_store(&client->stopping,true); } while(0);
    if(client->cleanup_live) {

        return true;
    }
    atomic_store(&client->cleanup_done,false);
    atomic_store(&client->cleanup_failed,false);

    ChiakiErrorCode result=chiaki_thread_create(&client->cleanup_thread,cleanup_worker,client);

    client->cleanup_live=result==CHIAKI_ERR_SUCCESS;

    return client->cleanup_live;
}
bool rp_poll_stop(RpClient *client) {
    if(!atomic_load(&client->stopping)) return true;
    if(!client->cleanup_live || !atomic_load(&client->cleanup_done)) return false;

    ChiakiErrorCode result=chiaki_thread_timedjoin(&client->cleanup_thread,NULL,0);

    if(result!=CHIAKI_ERR_SUCCESS) return false;
    client->cleanup_live=false;

    atomic_store(&client->stopping,false);

    return true;
}
bool rp_stopping(RpClient *client) { return atomic_load(&client->stopping); }
bool rp_cleanup_failed(RpClient *client) { return atomic_load(&client->cleanup_failed); }
static void discovered(ChiakiDiscoveryHost *h, void *user) {
    RpClient *c=user;

    int current=rp_state(c);
    bool accepting=current==RP_DISCOVERING || (c->automatic_wake && current==RP_WAKING);
    const char *target=c->target_address[0]?c->target_address:CONSOLE_IP;
    if(!h->host_addr || strcmp(h->host_addr,target) || !accepting || rp_stopping(c)) {
         /* wrong source, late, or stopped */
        return;
    }
    bool ps5=chiaki_discovery_host_is_ps5(h);
    bool ps4=h->device_discovery_protocol_version &&
        !strcmp(h->device_discovery_protocol_version,CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4);
    ChiakiTarget target_type=CHIAKI_TARGET_PS5_1;
    if(ps4 && h->system_version && h->system_version[0])
        target_type=chiaki_discovery_host_system_version_target(h);
    if(c->adding_pair && ps4 && !pair_target_supported(target_type)) {
        fail(c,RP_ERROR_UNSUPPORTED); return;
    }
    if((!ps5 && !ps4) || (ps4 && (!h->system_version || !h->system_version[0])) ||
       !pair_target_supported(target_type) ||
       (!c->adding_pair && ps5 != (!c->have_saved || chiaki_target_is_ps5(c->saved.target))) ||
       !h->host_id || strlen(h->host_id)!=12 ||
       !h->host_name || !h->host_name[0] ||
       (!c->adding_pair && c->target_identity[0] && strcasecmp(h->host_id,c->target_identity))) {

         /* identity mismatch */
        int expected=current;
        if(atomic_compare_exchange_strong(&c->state,&expected,RP_WRONG_CONSOLE))
            atomic_store(&c->error,RP_ERROR_IDENTITY);
        return;
    }
    if(h->state!=CHIAKI_DISCOVERY_HOST_STATE_READY && h->state!=CHIAKI_DISCOVERY_HOST_STATE_STANDBY) {
         /* unknown state */
        return;
    }
    RpState next=h->state==CHIAKI_DISCOVERY_HOST_STATE_READY ? RP_FOUND:
        (c->automatic_wake ? RP_WAKING:RP_STANDBY);
    if(c->adding_pair) {
        snprintf(c->candidate.address,sizeof(c->candidate.address),"%s",h->host_addr);
        snprintf(c->target_identity,sizeof(c->target_identity),"%.16s",h->host_id);
        for(size_t i=0;c->target_identity[i];i++) if(c->target_identity[i]>='a'&&c->target_identity[i]<='f') c->target_identity[i]-='a'-'A';
        snprintf(c->target_nickname,sizeof(c->target_nickname),"%.31s",h->host_name);
        c->candidate.target=target_type; c->candidate.key_type=2;
    }
    /* Publish verification before FOUND lets the startup worker connect. */
    atomic_store(&c->verified,true);
    if(rp_stopping(c) || !atomic_compare_exchange_strong(&c->state,&current,next)) {
         /* timed out/stopped while callback was running */
        return;
    }

}
static bool discovery_send_probe(RpClient *c, unsigned ordinal) {
    RpState state=rp_state(c);
    if(rp_stopping(c) || (state!=RP_DISCOVERING && state!=RP_WAKING) || !c->discovery_live) {
        return true;
    }
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET;
    inet_pton(AF_INET,c->target_address[0]?c->target_address:CONSOLE_IP,&addr.sin_addr);
    bool ps5=!c->have_saved || chiaki_target_is_ps5(c->saved.target);
    bool sent=false;
    ChiakiErrorCode e=CHIAKI_ERR_UNKNOWN;
    /* Pairing probes both families at the user-entered address only. Saved
     * consoles use only their registered family, including wake/fallback. */
    for(unsigned i=0;i<(c->adding_pair?2u:1u);i++) {
        bool probe_ps5=c->adding_pair ? i==0 : ps5;
        addr.sin_port=htons(probe_ps5?CHIAKI_DISCOVERY_PORT_PS5:CHIAKI_DISCOVERY_PORT_PS4);
        ChiakiDiscoveryPacket packet={.cmd=CHIAKI_DISCOVERY_CMD_SRCH,
            .protocol_version=probe_ps5?CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5:CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4};
        e=chiaki_discovery_send(&c->discovery,&packet,(struct sockaddr *)&addr,sizeof(addr));
        sent |= e==CHIAKI_ERR_SUCCESS;
    }

    if(!sent) { if(!c->automatic_wake) fail(c,error_code(e)); return false; }
    return true;
}

/* Chiaki stores the registration credential in a 16-byte, NUL-padded field.
 * Wakeup uses the short hexadecimal text form (at most eight characters),
 * interpreted as an integer and emitted as decimal by discovery.c. */
static bool wakeup_credential(const uint8_t key[16], uint64_t *credential) {
    char text[9];
    size_t length=0;
    while(length<8 && key[length]) {
        unsigned char ch=key[length];
        if(!((ch>='0' && ch<='9') || (ch>='a' && ch<='f') || (ch>='A' && ch<='F')))
            return false;
        text[length]=(char)ch;
        length++;
    }
    if(!length || (length==8 && key[8])) return false;
    for(size_t i=length;i<16;i++) if(key[i]) return false;
    text[length]='\0';
    errno=0;
    char *end=NULL;
    unsigned long long value=strtoull(text,&end,16);
    if(errno || end!=text+length) return false;
    *credential=(uint64_t)value;
    return true;
}
static bool discover(RpClient *c) {
    if(rp_stopping(c)) return false;
    rp_stop(c); atomic_store(&c->error,RP_ERROR_NONE);
    if(atomic_load(&c->cleanup_failed)) { fail(c,RP_ERROR_PROTOCOL); return false; }
    atomic_store(&c->state,RP_DISCOVERING);
    ChiakiErrorCode e=chiaki_discovery_init(&c->discovery,&c->log,AF_INET);
    if(e!=CHIAKI_ERR_SUCCESS) { if(!c->automatic_wake) fail(c,error_code(e)); return false; }
    atomic_store(&c->state,RP_DISCOVERING);
    e=chiaki_discovery_thread_start(&c->discovery_thread,&c->discovery,discovered,c);
    if(e!=CHIAKI_ERR_SUCCESS) { chiaki_discovery_fini(&c->discovery); if(!c->automatic_wake) fail(c,error_code(e)); return false; }
    c->discovery_live=true;
    return discovery_send_probe(c,1);
}
bool rp_discover(RpClient *c) {
    if(rp_stopping(c) || c->startup_live) return false;
    c->automatic_wake=false;

    return discover(c);
}
static bool discovery_finish(RpClient *c) {
    if(c->discovery_live) {
        ChiakiErrorCode result=chiaki_discovery_thread_stop(&c->discovery_thread);
        if(result!=CHIAKI_ERR_SUCCESS) { fail(c,error_code(result)); return false; }
        chiaki_discovery_fini(&c->discovery);
        c->discovery_live=false;
    }
    return true;
}
static void registered(ChiakiRegistEvent *event, void *user) {
    RpClient *c=user;

    if(event->type==CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS && event->registered_host) {
        const ChiakiRegisteredHost *h=event->registered_host;
        if(h->target!=c->candidate.target || !pair_key_supported(h->target,h->rp_key_type) ||
           (c->target_nickname[0] && strncmp(h->server_nickname,c->target_nickname,sizeof(h->server_nickname)))) {
            fail(c,RP_ERROR_IDENTITY); return;
        }
        c->candidate.target=h->target; c->candidate.key_type=h->rp_key_type;
        memcpy(c->candidate.regist_key,h->rp_regist_key,16); memcpy(c->candidate.key,h->rp_key,16);
        memcpy(c->candidate.mac,h->server_mac,6);
        if(!c->candidate.identity[0]) snprintf(c->candidate.identity,sizeof(c->candidate.identity),"%s",RP_CONSOLE_ID);
        if(!c->candidate.nickname[0]) snprintf(c->candidate.nickname,sizeof(c->candidate.nickname),"%s",RP_CONSOLE_NAME);
        c->candidate.generation=chiaki_random_32();
        /* State is the release/acquire publication barrier for candidate. */
        atomic_store(&c->state,RP_PAIRED);
    } else if(event->type==CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED) atomic_store(&c->state,RP_STOPPED);
    else {
        RpError error=error_code(event->error);
        if(event->application_reason==CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID) error=RP_ERROR_ACCOUNT;
        else if(event->application_reason==CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED) error=RP_ERROR_CODE;
        fail(c,error);
    }
}
bool rp_pair(RpClient *c, const uint8_t account[8], const char *pin) {
    if(rp_state(c)!=RP_FOUND || !atomic_load(&c->verified) || !account || !digits(pin,8)) {
        fail(c,RP_ERROR_PROTOCOL); return false;
    }
    if(!discovery_finish(c)) return false;
    ChiakiTarget target=c->adding_pair ? c->candidate.target : (c->have_saved ? c->saved.target : CHIAKI_TARGET_PS5_1);
    if(!pair_target_supported(target)) { fail(c,RP_ERROR_PROTOCOL); return false; }
    secret_clear(&c->candidate,sizeof(c->candidate));
    snprintf(c->candidate.address,sizeof(c->candidate.address),"%s",c->target_address[0]?c->target_address:CONSOLE_IP);
    memcpy(c->candidate.account,account,8);
    if(c->adding_pair) {
        snprintf(c->candidate.identity,sizeof(c->candidate.identity),"%s",c->target_identity);
        snprintf(c->candidate.nickname,sizeof(c->candidate.nickname),"%s",c->target_nickname);
        c->candidate.target=target;c->candidate.key_type=2;
    }
    c->candidate.target=target;
    ChiakiRegistInfo info={0}; info.target=target; info.host=c->candidate.address;
    info.pin=(uint32_t)strtoul(pin,NULL,10); memcpy(info.psn_account_id,account,8);
    atomic_store(&c->error,RP_ERROR_NONE); atomic_store(&c->state,RP_PAIRING);
    ChiakiErrorCode e=chiaki_regist_start(&c->registration,&c->log,&info,registered,c);
    secret_clear(&info,sizeof(info));
    if(e!=CHIAKI_ERR_SUCCESS) { fail(c,error_code(e)); return false; }
    c->registration_live=true; return true;
}
bool rp_save_pair(RpClient *c, const char *path) {
    if(rp_state(c)!=RP_PAIRED || !c->candidate.identity[0]) return false;
    /* Join before retiring the PIN and before saving the callback's output. */
    if(c->registration_live) {
        chiaki_regist_fini(&c->registration); secret_clear(&c->registration,sizeof(c->registration));
        c->registration_live=false;
    }
    c->have_candidate=true;
    if(!pair_store_save(path,&c->candidate)) { atomic_store(&c->error,RP_ERROR_SAVE); return false; }
    c->saved=c->candidate; c->have_saved=true;
    secret_clear(&c->candidate,sizeof(c->candidate)); c->have_candidate=false;
    atomic_store(&c->error,RP_ERROR_NONE); return true;
}
StoreResult rp_load_pair(RpClient *c, const char *path) {
    PairRecord r={0}; StoreResult result=pair_store_load(path,&r);
    if(result==STORE_OK) { rp_stop(c); c->saved=r; c->have_saved=true;
        snprintf(c->target_address,sizeof(c->target_address),"%s",r.address);
        snprintf(c->target_identity,sizeof(c->target_identity),"%s",r.identity);
        snprintf(c->target_nickname,sizeof(c->target_nickname),"%s",r.nickname);
        atomic_store(&c->state,RP_PAIRED); }
    secret_clear(&r,sizeof(r)); return result;
}
bool rp_load_record(RpClient *c,const PairRecord *record){
    if(!c||!record||!pair_key_supported(record->target,record->key_type)||record->legacy||!record->address[0]||!record->identity[0])return false;
    rp_stop(c);c->saved=*record;c->have_saved=true;c->adding_pair=false;
    snprintf(c->target_address,sizeof(c->target_address),"%s",record->address);
    snprintf(c->target_nickname,sizeof(c->target_nickname),"%s",record->nickname);
    snprintf(c->target_identity,sizeof(c->target_identity),"%s",record->identity);
    atomic_store(&c->state,RP_PAIRED);return true;
}
void rp_clear_active_record(RpClient *c){if(!c)return;rp_stop(c);secret_clear(&c->saved,sizeof(c->saved));c->have_saved=false;c->target_address[0]=c->target_identity[0]=c->target_nickname[0]=0;atomic_store(&c->state,RP_IDLE);}
bool rp_set_pair_target(RpClient *c,const char *address,const char *nickname){
    struct in_addr parsed;if(!c||!address||inet_pton(AF_INET,address,&parsed)!=1||rp_stopping(c)||c->startup_live||c->discovery_live)return false;
    snprintf(c->target_address,sizeof(c->target_address),"%s",address);
    snprintf(c->target_nickname,sizeof(c->target_nickname),"%s",nickname?nickname:"");
    c->target_identity[0]=0;c->adding_pair=true;secret_clear(&c->candidate,sizeof(c->candidate));return true;
}
bool rp_candidate_record(RpClient *c,PairRecord *record){if(!c||!record||rp_state(c)!=RP_PAIRED||!c->candidate.identity[0])return false;if(c->registration_live){chiaki_regist_fini(&c->registration);secret_clear(&c->registration,sizeof(c->registration));c->registration_live=false;}c->have_candidate=true;*record=c->candidate;return true;}
bool rp_forget(RpClient *c, const char *path) {
    rp_stop(c);
    if(!store_remove(path)) return false;
    secret_clear(&c->saved,sizeof(c->saved)); c->have_saved=false;
    atomic_store(&c->error,RP_ERROR_NONE); atomic_store(&c->state,RP_IDLE); return true;
}
static void session_event(ChiakiEvent *event, void *user) {
    RpClient *c=user;

    switch(event->type) {
        case CHIAKI_EVENT_CONNECTED:
            if(rp_error(c)!=RP_ERROR_IDENTITY) atomic_store(&c->state,RP_CONNECTED);
            break;
        case CHIAKI_EVENT_NICKNAME_RECEIVED:
            if(c->target_nickname[0]&&strncmp(event->server_nickname,c->target_nickname,32)) fail(c,RP_ERROR_IDENTITY);
            break;
        case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
            atomic_store(&c->error,event->login_pin_request.pin_incorrect ? RP_ERROR_LOGIN:RP_ERROR_NONE);
            atomic_store(&c->state,RP_LOGIN_PIN); break;
        case CHIAKI_EVENT_QUIT:
            if(rp_error(c)==RP_ERROR_IDENTITY) break;
            if(!chiaki_quit_reason_is_error(event->quit.reason)) atomic_store(&c->state,RP_STOPPED);
            else fail(c,event->quit.reason==CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE ? RP_ERROR_BUSY:RP_ERROR_NETWORK);
            break;
        default: break;
    }
}
static bool video_bridge(uint8_t *buf, size_t size, int32_t frames_lost, bool recovered, void *user) {
    (void)recovered;
    RpClient *c=user;
    if(rp_stopping(c) || !c->video_cb) return false;
    unsigned w=0, h=0;
    ChiakiVideoReceiver *r=c->session.stream_connection.video_receiver;
    if(r && r->profile_cur>=0 && (size_t)r->profile_cur<r->profiles_count) {
        w=r->profiles[r->profile_cur].width;
        h=r->profiles[r->profile_cur].height;
    }
    bool accepted=c->video_cb(buf,size,w,h,frames_lost,c->video_cb_user);
    uint32_t now=(uint32_t)chiaki_time_now_monotonic_ms();
    if(!accepted && !rp_stopping(c) && (!c->last_idr_request || now-c->last_idr_request>=1000)) {
        c->last_idr_request=now;
        chiaki_session_request_idr(&c->session);
    }
    return accepted;
}
void rp_set_video_callback(RpClient *c, RpVideoCallback cb, void *user) {
    c->video_cb=cb; c->video_cb_user=user;
}
bool rp_set_controller(RpClient *c, const ChiakiControllerState *state) {
    if(!c || !state || atomic_load(&c->stopping) || rp_state(c)!=RP_CONNECTED) return false;
    /* Submission and rp_request_stop share the UI thread. The session and its
     * feedback mutex remain alive until that caller has stopped submitting. */
    ChiakiControllerState copy=*state;
    ChiakiErrorCode result=chiaki_session_set_controller_state(&c->session,&copy);

    return result==CHIAKI_ERR_SUCCESS;
}
static void rp_goto_bed_sent(void *user, ChiakiErrorCode result) {
    RpClient *c=user;
    atomic_store(&c->rest_mode_send_result,
        result==CHIAKI_ERR_SUCCESS ? 2 : -1);
}
bool rp_goto_bed(RpClient *c) {
    if(!c || !c->session_started || atomic_load(&c->stopping) ||
       rp_state(c)!=RP_CONNECTED) return false;
    atomic_store(&c->rest_mode_send_result,1);
    ChiakiErrorCode result=chiaki_session_goto_bed_cb(&c->session,
        rp_goto_bed_sent,c);
    if(result!=CHIAKI_ERR_SUCCESS) {
        atomic_store(&c->rest_mode_send_result,-1);
        return false;
    }
    return true;
}
int rp_goto_bed_result(RpClient *c) {
    if(!c) return -1;
    int result=atomic_load(&c->rest_mode_send_result);
    return result==2 ? 1 : result<0 ? -1 : 0;
}
bool rp_request_idr(RpClient *c) {
    return c->session_started && chiaki_session_request_idr(&c->session)==CHIAKI_ERR_SUCCESS;
}
int32_t rp_frames_lost(RpClient *c) {
    if(!c->session_started) return 0;
    ChiakiVideoReceiver *r=c->session.stream_connection.video_receiver;
    return r ? chiaki_video_receiver_get_frames_lost_total(r) : 0;
}
AudioStats rp_audio_stats(RpClient *c) {
    if(c && c->audio) return audio_pipeline_stats(c->audio);
    AudioStats unavailable={0};
    unavailable.failed=true;
    unavailable.last_error=-1205; /* pipeline allocation unavailable */
    return unavailable;
}
bool rp_connect(RpClient *c) {
    if(rp_stopping(c)) return false;
    if(!app_settings_valid(&c->stream_settings)) { fail(c,RP_ERROR_PROTOCOL); return false; }
    if(!c->have_saved || !atomic_load(&c->verified) || rp_state(c)!=RP_FOUND) return false;

    if(!discovery_finish(c)) return false;

    ChiakiConnectInfo info={0}; info.ps5=chiaki_target_is_ps5(c->saved.target); info.host=c->saved.address;
    memcpy(info.regist_key,c->saved.regist_key,16); memcpy(info.morning,c->saved.key,16);
    memcpy(info.psn_account_id,c->saved.account,8);
    chiaki_connect_video_profile_preset(&info.video_profile,
        CHIAKI_VIDEO_RESOLUTION_PRESET_360p,
        CHIAKI_VIDEO_FPS_PRESET_30);
    info.video_profile.bitrate=2000;
    info.video_profile.codec=CHIAKI_CODEC_H264;
    /* The request mask is copied by session_init, so establish the output
     * pipeline first. A failed audio start leaves video enabled. */
    c->audio_started=c->audio && audio_pipeline_start(c->audio);
    info.audio_video_disabled=c->audio_started ? 0 : CHIAKI_AUDIO_DISABLED;

    info.enable_idr_on_fec_failure=true;
    info.packet_loss_max=0.05;

    ChiakiErrorCode e=chiaki_session_init(&c->session,&info,&c->log);
    secret_clear(&info,sizeof(info));
    if(e!=CHIAKI_ERR_SUCCESS) {
        if(c->audio_started) {
            audio_pipeline_stop(c->audio);
            while(!audio_pipeline_join(c->audio)) usleep(2000);
            c->audio_started=false;
        }
        fail(c,error_code(e)); return false;
    }

    c->session_initialized=true;
    chiaki_session_set_event_cb(&c->session,session_event,c);
    chiaki_session_set_video_sample_cb(&c->session,video_bridge,c);
    if(c->audio_started) {
        ChiakiAudioSink sink={0};
        audio_pipeline_get_sink(c->audio,&sink);
        chiaki_session_set_audio_sink(&c->session,&sink);
    }
    atomic_store(&c->error,RP_ERROR_NONE); atomic_store(&c->state,RP_CONNECTING);
    e=chiaki_session_start(&c->session);
    if(e!=CHIAKI_ERR_SUCCESS) { rp_stop(c); fail(c,error_code(e)); return false; }

    c->session_started=true; return true;
}
/* Discovery is advisory for saved pairings. Never bypass an explicit identity
 * mismatch or a cancellation; direct sessions still authenticate with saved keys. */
static void connect_after_discovery(RpClient *client) {
    if(rp_stopping(client)) return;
    if(!discovery_finish(client)) return;
    int state=rp_state(client);
    if(rp_stopping(client) ||
       (state!=RP_DISCOVERING && state!=RP_WAKING && state!=RP_FOUND)) return;
    if(state!=RP_FOUND) {
         /* direct fallback, no secrets */
        atomic_store(&client->verified,true);
        if(!atomic_compare_exchange_strong(&client->state,&state,RP_FOUND)) return;
    }
    if(!rp_stopping(client)) rp_connect(client);
}
static void try_saved_wake(RpClient *client) {
    RpState state=rp_state(client);
    if(rp_stopping(client) ||
       (state!=RP_DISCOVERING && state!=RP_WAKING)) return;
    uint64_t credential=0;
    if(!wakeup_credential(client->saved.regist_key,&credential)) {

        return; /* A bad wake credential must not block an awake console. */
    }
    (void)chiaki_discovery_wakeup(&client->log,
        client->discovery_live?&client->discovery:NULL,client->saved.address,credential,chiaki_target_is_ps5(client->saved.target));

}
static void *startup_worker(void *user) {
    RpClient *client=user;

    uint32_t start=(uint32_t)chiaki_time_now_monotonic_ms();
    if(!discover(client)) {
        try_saved_wake(client);
        connect_after_discovery(client);
        return NULL;
    }
    unsigned probes=1;
    bool wake_sent=false, wake_timer_started=false;
    uint32_t wake_start=0,last_probe=start;
    while(!rp_stopping(client)) {
        RpState state=rp_state(client);
        if(state!=RP_DISCOVERING && state!=RP_WAKING) break;
        uint32_t now=(uint32_t)chiaki_time_now_monotonic_ms(),elapsed=now-start;
        /* A forwarded endpoint may accept wake packets without replying to
         * discovery. Try wake once even when status is unavailable. */
        if(!wake_sent && (state==RP_WAKING || elapsed>=1000)) {
            try_saved_wake(client);
            wake_sent=true;
        }
        if(state==RP_DISCOVERING) {
            if(elapsed>=5000) break;
            if(probes<5 && elapsed>=probes*1000) {
                if(!discovery_send_probe(client,++probes)) break;
                last_probe=now;
            }
        } else {
            if(!wake_timer_started) { wake_start=now; wake_timer_started=true; }
            if((uint32_t)(now-wake_start)>=RP_WAKE_TIMEOUT_MS) break;
            if((uint32_t)(now-last_probe)>=1000) {
                if(!discovery_send_probe(client,++probes)) break;
                last_probe=now;
            }
        }
        usleep(2000);
    }
    connect_after_discovery(client);
    return NULL;
}
bool rp_set_stream_settings(RpClient *client, const AppSettings *settings) {
    if(!client || !app_settings_valid(settings) || rp_stopping(client) ||
       client->startup_live || client->cleanup_live || client->session_initialized ||
       client->session_started || client->discovery_live || client->registration_live ||
       client->audio_started || atomic_load(&client->cleanup_failed)) return false;
    RpState state=rp_state(client);
    if(state!=RP_IDLE && state!=RP_PAIRED && state!=RP_STOPPED && state!=RP_FAILED) return false;
    client->stream_settings=*settings;
    return true;
}
bool rp_start_attempt(RpClient *client) {
    if(rp_stopping(client) || client->startup_live || client->session_started || !client->have_saved) return false;
    if(!app_settings_valid(&client->stream_settings)) return false;

    client->automatic_wake=true;
    atomic_store(&client->cleanup_done,false);
    client->last_idr_request=0;
    atomic_store(&client->cleanup_failed,false);
    ChiakiErrorCode result=chiaki_thread_create(&client->cleanup_thread,cleanup_worker,client);
    client->cleanup_live=result==CHIAKI_ERR_SUCCESS;
    if(!client->cleanup_live) { fail(client,RP_ERROR_PROTOCOL); return false; }
    atomic_store(&client->state,RP_DISCOVERING);
    result=chiaki_thread_create(&client->startup_thread,startup_worker,client);
    client->startup_live=result==CHIAKI_ERR_SUCCESS;
    if(!client->startup_live) fail(client,RP_ERROR_PROTOCOL);
    return client->startup_live;
}
bool rp_login_pin(RpClient *c, const char *pin) {
    if(rp_state(c)!=RP_LOGIN_PIN || !digits(pin,4)) return false;
    atomic_store(&c->state,RP_CONNECTING); atomic_store(&c->error,RP_ERROR_NONE);
    ChiakiErrorCode e=chiaki_session_set_login_pin(&c->session,(const uint8_t *)pin,4);
    if(e!=CHIAKI_ERR_SUCCESS) { fail(c,error_code(e)); return false; } return true;
}
RpClient *rp_create(void) {
    if(chiaki_lib_init()!=CHIAKI_ERR_SUCCESS) return NULL;
    RpClient *c=calloc(1,sizeof(*c)); if(!c) return NULL;
    app_settings_defaults(&c->stream_settings);
    atomic_init(&c->state,RP_IDLE); atomic_init(&c->error,RP_ERROR_NONE); atomic_init(&c->verified,false);
    atomic_init(&c->rest_mode_send_result,0);
    atomic_init(&c->stopping,false); atomic_init(&c->cleanup_done,false); atomic_init(&c->cleanup_failed,false);
    chiaki_log_init(&c->log, 0, NULL, NULL);
    c->audio=audio_pipeline_create(&c->log);

    return c;
}
void rp_destroy(RpClient *c) {
    if(c) {
        if(rp_stopping(c) || c->startup_live || c->cleanup_live) return;
        rp_stop(c);
        if(atomic_load(&c->cleanup_failed)) return;
        audio_pipeline_destroy(c->audio);
        secret_clear(c,sizeof(*c));
        free(c);
    }
}
RpState rp_state(RpClient *c) { return atomic_load(&c->state); }
RpError rp_error(RpClient *c) { return atomic_load(&c->error); }
bool rp_has_pair(const RpClient *c) { return c->have_saved; }
bool rp_has_candidate(const RpClient *c) { return c->have_candidate; }
bool rp_pair_is_legacy(const RpClient *c) { return c->have_saved && c->saved.legacy; }
bool rp_saved_identity(const RpClient *c) { return c->have_saved && !c->saved.legacy && !strcmp(c->saved.identity,c->target_identity); }
const char *rp_error_message(RpError e) {
    switch(e) {
        case RP_ERROR_UNSUPPORTED: return "PS4 system software 8.0 or newer is\nrequired by this build.";
        case RP_ERROR_ACCOUNT: return "Account ID rejected. Use the ID for the\nconsole user you want to play as. Edit account\nand enter a fresh Remote Play code.";
        case RP_ERROR_CODE: return "Pairing rejected. Reopen Remote Play on\nthe console and enter its new 8-digit code.";
        case RP_ERROR_NETWORK: return "Could not connect. Turn the console on,\nenable Remote Play and check that both\ndevices use the same network. Retry.";
        case RP_ERROR_TIMEOUT: return "No answer in time. Check Wi-Fi and the\nconsole. For pairing, reopen Remote Play\nand enter a fresh code.";
        case RP_ERROR_WAKE_TIMEOUT: return "The console did not wake in time. Check its\nnetwork and Remote Play settings, then\ntry again.";
        case RP_ERROR_WAKE: return "Could not send the console wake request.\nCheck Wi-Fi and retry.";
        case RP_ERROR_WAKE_CREDENTIAL: return "Saved wake credential is invalid. Your\nsaved pairing is unchanged.";
        case RP_ERROR_IDENTITY: return "This address did not identify the selected\nconsole. Check its network address. Your saved\npairing has not been replaced.";
        case RP_ERROR_BUSY: return "Remote Play is in use. End the other\nsession, then retry this connection.";
        case RP_ERROR_LOGIN: return "Login passcode rejected. Enter the\n4-digit login passcode for your console user.";
        case RP_ERROR_SAVE: return "Paired, but SD save failed. Check free\nspace and retry Save. Previous saved\npairing remains available.";
        case RP_ERROR_PROTOCOL: return "No specific console error. Check the IP and\nAccount ID, then reopen Remote Play for a\nfresh 8-digit code.";
        default: return "";
    }
}
const char *rp_error_text(RpClient *c) { return rp_error_message(rp_error(c)); }

uint32_t rp_pair_generation(const RpClient *c) { return c->saved.generation; }
bool rp_upgrade_legacy(RpClient *c, const char *path) {
    if(!c->have_saved || rp_state(c)!=RP_CONNECTED) return false;
    if(!c->saved.legacy) return true;
    PairRecord upgraded=c->saved;
    upgraded.legacy=false;
    snprintf(upgraded.address,sizeof(upgraded.address),"%s",CONSOLE_IP);
    snprintf(upgraded.identity,sizeof(upgraded.identity),"%s",RP_CONSOLE_ID);
    snprintf(upgraded.nickname,sizeof(upgraded.nickname),"%s",RP_CONSOLE_NAME);
    upgraded.generation=chiaki_random_32();
    bool ok=pair_store_save(path,&upgraded);
    if(ok) c->saved=upgraded;
    secret_clear(&upgraded,sizeof(upgraded)); return ok;
}
