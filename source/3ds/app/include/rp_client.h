#ifndef N3DSRP_RP_CLIENT_H
#define N3DSRP_RP_CLIENT_H
#include "app.h"
#include "pair_store.h"
#include "app_settings.h"
#include "audio_pipeline.h"
#include <chiaki/regist.h>
#include <chiaki/session.h>
typedef enum {
    RP_IDLE, RP_DISCOVERING, RP_FOUND, RP_STANDBY, RP_WRONG_CONSOLE,
    RP_PAIRING, RP_PAIRED, RP_CONNECTING, RP_LOGIN_PIN, RP_CONNECTED,
    RP_FAILED, RP_STOPPED, RP_WAKING
} RpState;
typedef enum {
    RP_ERROR_NONE, RP_ERROR_NETWORK, RP_ERROR_TIMEOUT, RP_ERROR_ACCOUNT,
    RP_ERROR_CODE, RP_ERROR_IDENTITY, RP_ERROR_PROTOCOL, RP_ERROR_BUSY,
    RP_ERROR_LOGIN, RP_ERROR_SAVE, RP_ERROR_WAKE_TIMEOUT, RP_ERROR_WAKE,
    RP_ERROR_WAKE_CREDENTIAL, RP_ERROR_UNSUPPORTED
} RpError;
typedef struct RpClient RpClient;
RpClient *rp_create(void);
void rp_destroy(RpClient *c);
void rp_stop(RpClient *c);
/* UI thread only, before an attempt, with all workers fully stopped. */
bool rp_set_stream_settings(RpClient *client, const AppSettings *settings);
bool rp_start_attempt(RpClient *client);
/* Install on UI before attempts; immutable while workers are live. Called by
 * the serialized startup/cleanup owner during requested stop, after network
 * producers join and before session fini. Must poll actual media kernel joins;
 * false retains all resources. UI must cancel media before rp_request_stop. */
typedef bool (*RpMediaJoinCallback)(void *user);
void rp_set_media_join_callback(RpClient *client, RpMediaJoinCallback callback, void *user);
bool rp_request_stop(RpClient *client);
bool rp_poll_stop(RpClient *client);
bool rp_stopping(RpClient *client);
bool rp_cleanup_failed(RpClient *client);
/* UI thread only, while connected and before rp_request_stop. Returns true
 * when queued. The send result is completed asynchronously by the control
 * worker and can be queried with rp_goto_bed_result(). */
bool rp_goto_bed(RpClient *client);
/* 0 while pending/not requested; 1 when the transport send completed; -1 on
 * enqueue or send failure. This is send completion, not a PS5 acknowledgment. */
int rp_goto_bed_result(RpClient *client);
bool rp_discover(RpClient *c);
bool rp_pair(RpClient *c, const uint8_t account[8], const char *pin);
bool rp_connect(RpClient *c);
bool rp_login_pin(RpClient *c, const char *pin);
bool rp_save_pair(RpClient *c, const char *path);
StoreResult rp_load_pair(RpClient *c, const char *path);
bool rp_load_record(RpClient *c, const PairRecord *record);
void rp_clear_active_record(RpClient *c);
bool rp_set_pair_target(RpClient *c, const char *address, const char *nickname);
bool rp_candidate_record(RpClient *c, PairRecord *record);
bool rp_forget(RpClient *c, const char *path);
bool rp_has_pair(const RpClient *c);
bool rp_has_candidate(const RpClient *c);
bool rp_pair_is_legacy(const RpClient *c);
RpState rp_state(RpClient *c);
RpError rp_error(RpClient *c);
const char *rp_error_text(RpClient *c);
const char *rp_error_message(RpError error);
/* Non-secret metadata for evidence. Never exposes the record or key bytes. */
bool rp_upgrade_legacy(RpClient *c, const char *path);
uint32_t rp_pair_generation(const RpClient *c);
bool rp_saved_identity(const RpClient *c);

/* Video: whole access units (Annex-B) forwarded from chiaki's video receiver.
 * width/height are the negotiated profile geometry (0 when not yet known).
 * Runs on chiaki's stream thread -- return false to refuse and force a
 * keyframe request. */
typedef bool (*RpVideoCallback)(uint8_t *buf, size_t size, unsigned width,
                                unsigned height, int32_t frames_lost, void *user);
void rp_set_video_callback(RpClient *c, RpVideoCallback cb, void *user);
bool rp_request_idr(RpClient *c);
int32_t rp_frames_lost(RpClient *c);
/* Atomic snapshot; audio errors never make the video session fatal. */
AudioStats rp_audio_stats(RpClient *c);
/* The UI thread alone submits controller states and requests stop. Submit
 * states only during play; stop performs no feedback submission. Atomic state
 * and stop guards protect startup publication, while Chiaki's feedback sender
 * mutex protects the controller state during an active session. */
bool rp_set_controller(RpClient *c, const ChiakiControllerState *state);
#endif
