#ifndef RP_SETUP_H
#define RP_SETUP_H
#include "rp_client.h"
typedef enum {
    UI_CONSOLE, UI_SEARCH, UI_ACCOUNT, UI_CODE, UI_REVIEW, UI_PAIRING,
    UI_SAVE_FAILED, UI_READY, UI_CONNECTING, UI_LOGIN, UI_SUCCESS,
    UI_ERROR, UI_FORGET, UI_HELP
} SetupScreen;
typedef enum { ACTION_PRIMARY, ACTION_SECONDARY, ACTION_BACK, ACTION_FORGET, ACTION_HELP } SetupAction;
typedef enum { INTENT_SETUP, INTENT_PAIR, INTENT_CONNECT } SetupIntent;
typedef struct {
    RpClient *client;
    RpConfig config;
    SetupScreen screen, help_back;
    SetupIntent intent;
    double since;
    char pin[9];
    char message[224];
    bool loaded_at_launch, paired_this_launch, saved_this_launch, connected_this_launch;
    bool evidence_pending;
    unsigned connection_attempts;
} Setup;
void setup_init(Setup *s, RpClient *c);
void setup_action(Setup *s, SetupAction action, double now);
bool setup_text(Setup *s, const char *text, double now);
void setup_tick(Setup *s, double now);
void setup_cancel(Setup *s);
const char *setup_title(const Setup *s);
#endif
