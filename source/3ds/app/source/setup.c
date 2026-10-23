/* UI-independent workflow, exercised on the host with the same storage/client API. */
#include "setup.h"
#include <stdio.h>
static void message(Setup *s, const char *text) { snprintf(s->message,sizeof(s->message),"%s",text); }
static void screen(Setup *s, SetupScreen next, double now) { s->screen=next; s->since=now; }
static void error(Setup *s, const char *text, double now) {
    message(s,text); secret_clear(s->pin,sizeof(s->pin)); rp_stop(s->client); screen(s,UI_ERROR,now);
}
static void search(Setup *s, SetupIntent intent, double now) {
    s->intent=intent; message(s,"");
    if(!rp_discover(s->client)) error(s,rp_error_text(s->client),now);
    else screen(s,UI_SEARCH,now);
}
void setup_init(Setup *s, RpClient *c) {
    memset(s,0,sizeof(*s)); s->client=c;
    cfg_load(&s->config);
    snprintf(s->config.host,sizeof(s->config.host),"%s",RP_CONSOLE_IP);
    StoreResult r=rp_load_pair(c,RP_PAIR_FILE);
    s->loaded_at_launch=r==STORE_OK;
    s->screen=s->loaded_at_launch ? UI_READY:UI_CONSOLE;
    if(r==STORE_INVALID) message(s,"Saved pairing is damaged or belongs to\nanother console. It will be preserved\nuntil a new pairing is saved.");
    if(r==STORE_IO) message(s,"Could not read the SD pairing file.\nCheck your SD card before continuing.");
}
void setup_cancel(Setup *s) {
    rp_stop(s->client); secret_clear(s->pin,sizeof(s->pin));
    s->screen=rp_has_pair(s->client) ? UI_READY:UI_CONSOLE;
    message(s,rp_has_pair(s->client) ? "Stopped. Saved pairing kept.":"Canceled. No pairing saved.");
}
void setup_action(Setup *s, SetupAction a, double now) {
    if(a==ACTION_HELP && s->screen==UI_ACCOUNT) { s->help_back=s->screen; screen(s,UI_HELP,now); return; }
    if(s->screen==UI_HELP) { if(a==ACTION_BACK || a==ACTION_PRIMARY) screen(s,s->help_back,now); return; }
    if(a==ACTION_FORGET && (s->screen==UI_READY || s->screen==UI_SUCCESS) && rp_has_pair(s->client)) {
        screen(s,UI_FORGET,now); message(s,""); return;
    }
    if(s->screen==UI_FORGET) {
        if(a==ACTION_BACK) { screen(s,UI_READY,now); return; }
        if(a==ACTION_PRIMARY) {
            if(!rp_forget(s->client,RP_PAIR_FILE)) { message(s,"Could not remove pairing from SD.\nPairing retained. Retry or go back."); return; }
            secret_clear(s->config.account,8); s->config.have_account=false;
            s->loaded_at_launch=false; s->paired_this_launch=false; s->saved_this_launch=false;
            bool saved=cfg_save(&s->config);
            message(s,saved ? "Saved pairing removed.":"Pairing removed. Account settings could\nnot be cleared from SD. Check the card.");
            screen(s,UI_CONSOLE,now);
        }
        return;
    }
    if(a==ACTION_BACK) {
        switch(s->screen) {
            case UI_CODE: secret_clear(s->pin,sizeof(s->pin)); screen(s,UI_ACCOUNT,now); break;
            case UI_REVIEW: secret_clear(s->pin,sizeof(s->pin)); screen(s,UI_CODE,now); break;
            case UI_ACCOUNT: screen(s,UI_CONSOLE,now); break;
            default: setup_cancel(s); break;
        }
        return;
    }
    if(a==ACTION_SECONDARY) {
        if(s->screen==UI_READY || s->screen==UI_SUCCESS) search(s,INTENT_SETUP,now);
        else if(s->screen==UI_ERROR) { screen(s,UI_ACCOUNT,now); message(s,""); }
        return;
    }
    if(a!=ACTION_PRIMARY) return;
    switch(s->screen) {
        case UI_CONSOLE: search(s,INTENT_SETUP,now); break;
        case UI_REVIEW:
            if(now-s->since>120000) { secret_clear(s->pin,sizeof(s->pin)); screen(s,UI_CODE,now); message(s,"Code is old. Reopen Link Device and\nenter a fresh code."); }
            else search(s,INTENT_PAIR,now);
            break;
        case UI_SAVE_FAILED:
            if(rp_save_pair(s->client,RP_PAIR_FILE)) {
                s->saved_this_launch=true; screen(s,UI_READY,now); message(s,"Paired and saved. Ready to connect.");
            } else message(s,rp_error_message(RP_ERROR_SAVE));
            break;
        case UI_READY: case UI_SUCCESS: search(s,INTENT_CONNECT,now); break;
        case UI_ERROR:
            if(s->intent==INTENT_CONNECT) search(s,INTENT_CONNECT,now);
            else if(s->intent==INTENT_SETUP) search(s,INTENT_SETUP,now);
            else { screen(s,UI_CODE,now); message(s,"Enter a fresh code from Link Device."); }
            break;
        default: break; /* account/code/login input uses setup_text */
    }
}
bool setup_text(Setup *s, const char *text, double now) {
    if(!text) return false;
    if(s->screen==UI_ACCOUNT) {
        RpConfig proposed=s->config;
        if(!cfg_decode_account(text,proposed.account)) { message(s,"Enter the 12-character Base64 account\nID (including =), not your username.\nPress Y for help."); return false; }
        proposed.have_account=true;
        if(!cfg_save(&proposed)) { secret_clear(&proposed,sizeof(proposed)); message(s,"Could not save account settings to SD.\nCheck free space and retry."); return false; }
        s->config=proposed; secret_clear(&proposed,sizeof(proposed));
        message(s,""); screen(s,UI_CODE,now); return true;
    }
    if(s->screen==UI_CODE || s->screen==UI_LOGIN) {
        size_t n=s->screen==UI_CODE ? 8:4;
        if(strlen(text)!=n) { message(s,n==8 ? "The Link Device code must be 8 digits.":"The login passcode must be 4 digits."); return false; }
        for(size_t i=0;i<n;i++) if(text[i]<'0' || text[i]>'9') { message(s,"Use digits only."); return false; }
        if(s->screen==UI_CODE) {
            memcpy(s->pin,text,9); message(s,""); screen(s,UI_REVIEW,now);
        } else {
            if(!rp_login_pin(s->client,text)) { error(s,rp_error_text(s->client),now); return false; }
            message(s,""); screen(s,UI_CONNECTING,now);
        }
        return true;
    }
    return false;
}
void setup_tick(Setup *s, double now) {
    RpState st=rp_state(s->client);
    if(s->screen==UI_SEARCH) {
        if(st==RP_FOUND) {
            if(s->intent==INTENT_SETUP) { screen(s,UI_ACCOUNT,now); message(s,""); }
            else if(s->intent==INTENT_PAIR) {
                bool ok=rp_pair(s->client,s->config.account,s->pin);
                secret_clear(s->pin,sizeof(s->pin));
                if(ok) screen(s,UI_PAIRING,now); else error(s,rp_error_text(s->client),now);
            } else {
                s->connection_attempts++;
                if(rp_connect(s->client)) screen(s,UI_CONNECTING,now);
                else error(s,rp_error_text(s->client),now);
            }
        } else if(st==RP_STANDBY) error(s,"PS5-Example is in rest mode. Turn it on\nwith its controller, then retry.",now);
        else if(st==RP_WRONG_CONSOLE || st==RP_FAILED) error(s,rp_error_text(s->client),now);
        else if(now-s->since>5000) error(s,rp_error_message(RP_ERROR_TIMEOUT),now);
    } else if(s->screen==UI_PAIRING) {
        if(st==RP_PAIRED) {
            s->paired_this_launch=true;
            if(rp_save_pair(s->client,RP_PAIR_FILE)) {
                s->saved_this_launch=true; screen(s,UI_READY,now); message(s,"Paired and saved. Ready to connect.");
            } else { screen(s,UI_SAVE_FAILED,now); message(s,rp_error_message(RP_ERROR_SAVE)); }
        } else if(st==RP_FAILED || st==RP_STOPPED) error(s,rp_error_text(s->client),now);
        else if(now-s->since>15000) error(s,rp_error_message(RP_ERROR_TIMEOUT),now);
    } else if(s->screen==UI_CONNECTING) {
        if(st==RP_CONNECTED) {
            if(!rp_upgrade_legacy(s->client,RP_PAIR_FILE)) { error(s,"Connected, but pairing metadata could\nnot be saved to SD. Check space and\nretry the connection.",now); return; }
            s->connected_this_launch=true; s->evidence_pending=true;
            screen(s,UI_SUCCESS,now); message(s,"");
        } else if(st==RP_LOGIN_PIN) { screen(s,UI_LOGIN,now); message(s,rp_error_text(s->client)); }
        else if(st==RP_FAILED || st==RP_STOPPED) error(s,rp_error_text(s->client),now);
        else if(now-s->since>30000) error(s,"Connection timed out. Your pairing is\nstill saved. Retry Connect; a fresh code\nis not needed just for this timeout.",now);
    } else if(s->screen==UI_SUCCESS && now-s->since>2000) {
        /* A short authenticated connection is all Phase 1 needs. */
        rp_stop(s->client);
    }
}
const char *setup_title(const Setup *s) {
    static const char *titles[]={"1 / 3  Choose your PS5", "Finding PS5-Example", "2 / 3  Your PS5 account", "3 / 3  Link Device code", "Ready to pair", "Pairing with PS5-Example", "Paired - save needed", "Ready to connect", "Connecting securely", "PS5 login passcode", "Connection verified", "Let's fix that", "Forget saved pairing?", "Find your account ID"};
    return titles[s->screen];
}
