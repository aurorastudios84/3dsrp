#include <3ds.h>
#include <malloc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <chiaki/time.h>
#include "app.h"
#include "rp_client.h"
#include "video_pipeline.h"
#include "mvd_sink.h"
#include "input_3ds.h"
#include "motion_3ds.h"
#include "zoom_controls.h"
#include "app_settings.h"
#include "settings_writer.h"
#include "usage_stats.h"
#include "help_content.h"
#include "console_store.h"
#include "ui_skin.h"
#include "touch_trail.h"
#include "text_wrap.h"
#include <stdarg.h>
#include <arpa/inet.h>

/* Mirror menu output for presentation while retaining native console output. */
static int skin_printf(const char *format,...)
{
    char text[1024];va_list args;va_start(args,format);
    int n=vsnprintf(text,sizeof(text),format,args);va_end(args);
    printf("%s",text);ui_skin_write(text);return n;
}
static int skin_putchar(int ch)
{
    char text[2]={(char)ch,0};int result=putchar(ch);ui_skin_write(text);return result;
}
static int skin_fputs(const char *text,FILE *stream)
{
    int result=fputs(text,stream);if(stream==stdout) ui_skin_write(text);return result;
}
#undef printf
#undef putchar
#undef fputs
#define printf skin_printf
#define putchar skin_putchar
#define fputs skin_fputs

#define RP_SETTINGS_FILE RP_ROOT "settings.bin"

typedef enum { UI_READY, UI_DISCOVERY, UI_CONNECTING, UI_STREAM, UI_STOPPING, UI_DONE, UI_FLUSHING } Phase;
static RpClient *client;
static VideoPipeline *video;
static PrintConsole *bottom_console;
static Phase phase;
static uint32_t attempt, attempt_since, phase_since;
static bool exit_requested, cleanup_started, exit_after_disconnect;
static bool video_stop_requested;
static bool top_flush_pending,bottom_flush_pending;
static char status[160]="";
static char attempt_failure[96];
static Input3dsTouchCapture touch_capture;
static TouchTrail touch_trail;
static ChiakiControllerState last_controller;
static bool controller_valid;
static bool options_pulse_active;
static uint32_t options_pulse_started;
static ZoomControls zoom_controls;
static AppSettings app_settings;
static ConsoleStore consoles;
static char console_notice[120];
static unsigned console_scroll, console_remove_index;
static char pairing_ip[64], pairing_pin[9];
static uint8_t pairing_account[8];
static bool pairing_flow, pairing_wait_pin;
typedef enum { MENU_NONE, MENU_SETTINGS, MENU_HUD_HOLD, MENU_CONSOLES, MENU_HELP, MENU_SESSION, MENU_CONFIRM, MENU_CONSOLE_REMOVE_CONFIRM, MENU_USAGE } LocalMenu;
enum { SESSION_ITEM_COUNT=4 };
static LocalMenu local_menu, help_parent, confirm_parent;
enum { SETTINGS_SIXAXIS=APP_SETTING_BACK, SETTINGS_USAGE=APP_SETTING_COUNT, SETTINGS_BACK=APP_SETTING_COUNT+1, SETTINGS_COUNT=APP_SETTING_COUNT+2 };
static UsageStats usage;
static aptHookCookie usage_apt_hook;
static void usage_apt_event(APT_HookType event,void *unused)
{
    (void)unused;
    if(event==APTHOOK_ONSUSPEND || event==APTHOOK_ONSLEEP)
        usage_stats_suspend(&usage,(uint32_t)chiaki_time_now_monotonic_ms());
}

static UsagePreferences usage_preferences;
static LocalMenu usage_parent;
static unsigned usage_row;
static char usage_notice[80];
static bool sixaxis_enabled; /* App-lifetime only; OFF on every launch. */
static bool confirm_exit, controller_view=false;
static LocalMenu settings_parent;
static bool rest_mode_requested, rest_mode_queue_failed;
static bool rest_mode_action_pending, rest_mode_exit_after_action;
static unsigned menu_row, ready_row, help_page;
static AppSettings settings_draft;
static char recovery_notice[80];
static unsigned settings_row;
static unsigned hud_hold_row;
static const char *const hud_hold_labels[]={"L","ZL","R","ZR","D-pad Up","D-pad Down","D-pad Left","D-pad Right","A","B","X","Y"};
static const uint16_t hud_hold_bits[]={RP_INPUT_L,RP_INPUT_ZL,RP_INPUT_R,RP_INPUT_ZR,
    RP_INPUT_DPAD_UP,RP_INPUT_DPAD_DOWN,RP_INPUT_DPAD_LEFT,RP_INPUT_DPAD_RIGHT,
    RP_INPUT_A,RP_INPUT_B,RP_INPUT_X,RP_INPUT_Y};
static char settings_notice[80];
static bool live_active;
static bool zoom_settings_pending;
static uint32_t zoom_settings_changed_at;
static SettingsWriter *settings_writer;
static uint32_t zoom_save_revision, menu_save_revision;
static bool menu_save_has_crops, settings_writer_closed;

#ifdef N3DS_GPU_BILINEAR
#define TOP_FRAMEBUFFER_BYTES (GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_TOP * 3u)
#else
#define TOP_FRAMEBUFFER_BYTES (GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_TOP * 2u)
#endif
#define BOTTOM_FRAMEBUFFER_BYTES (GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_BOTTOM * 2u)

static Input3dsSnapshot sample_input(uint32_t held)
{
    Input3dsSnapshot snapshot={0};
    circlePosition circle={0},cstick={0};
    touchPosition touch={0};
    snapshot.motion=true;
    snapshot.held=((held&KEY_A)?RP_INPUT_A:0)|((held&KEY_B)?RP_INPUT_B:0)|
        ((held&KEY_X)?RP_INPUT_X:0)|((held&KEY_Y)?RP_INPUT_Y:0)|
        ((held&KEY_DUP)?RP_INPUT_DPAD_UP:0)|((held&KEY_DDOWN)?RP_INPUT_DPAD_DOWN:0)|
        ((held&KEY_DLEFT)?RP_INPUT_DPAD_LEFT:0)|((held&KEY_DRIGHT)?RP_INPUT_DPAD_RIGHT:0)|
        ((held&KEY_L)?RP_INPUT_L:0)|((held&KEY_R)?RP_INPUT_R:0)|
        ((held&KEY_ZL)?RP_INPUT_ZL:0)|((held&KEY_ZR)?RP_INPUT_ZR:0)|
        ((held&KEY_START)?RP_INPUT_START:0)|((held&KEY_SELECT)?RP_INPUT_SELECT:0);
    hidCircleRead(&circle); hidCstickRead(&cstick);
    snapshot.circle_x=circle.dx; snapshot.circle_y=circle.dy;
    snapshot.cstick_x=cstick.dx; snapshot.cstick_y=cstick.dy;
    snapshot.touch_down=(held&KEY_TOUCH)!=0;
    if(snapshot.touch_down) { hidTouchRead(&touch); snapshot.touch_x=touch.px; snapshot.touch_y=touch.py; }
    return snapshot;
}

static void submit_controller(const Input3dsSnapshot *snapshot)
{
    ChiakiControllerState current;
    input_3ds_map(snapshot,&touch_capture,&current);
    if(snapshot->motion) motion_3ds_apply_current(&current);
    if(controller_valid && chiaki_controller_state_equals(&current,&last_controller)) return;
    if(rp_set_controller(client,&current)) {

        last_controller=current; controller_valid=true;

    }
}

static void release_options_pulse(void)
{
    if(!options_pulse_active) return;
    options_pulse_active=false;
    Input3dsSnapshot neutral={0};
    submit_controller(&neutral);
}

static void push_options_button(void)
{
    ChiakiControllerState state;
    chiaki_controller_state_set_idle(&state);
    state.buttons=CHIAKI_CONTROLLER_BUTTON_OPTIONS;
    if(rp_set_controller(client,&state)) {
        last_controller=state;
        controller_valid=true;
        options_pulse_active=true;
        options_pulse_started=(uint32_t)chiaki_time_now_monotonic_ms();

    }
}

static void console_at(unsigned row,unsigned column) { printf("\x1b[%u;%uH",row+1,column+1);ui_skin_at(row,column); }
/* Subtle touch affordances composited over the HUD crop. The framebuffer is
 * column-major and vertically reversed, matching zoom_render_frame(). */
static uint16_t blend_white(uint16_t pixel,unsigned opacity)
{
    unsigned r=(pixel>>11)&31u,g=(pixel>>5)&63u,b=pixel&31u;
    r+=(31u-r)*opacity/255u; g+=(63u-g)*opacity/255u; b+=(31u-b)*opacity/255u;
    return (uint16_t)((r<<11)|(g<<5)|b);
}
static void hud_touch_pixel(uint16_t *fb,unsigned x,unsigned y,unsigned opacity)
{
    if(x<320 && y<240) { unsigned i=x*240u+239u-y; fb[i]=blend_white(fb[i],opacity); }
}
static void hud_touch_line(uint16_t *fb,unsigned x0,unsigned y0,unsigned x1,unsigned y1)
{
    if(x0==x1) for(unsigned y=y0;y<=y1;y++) hud_touch_pixel(fb,x0,y,72);
    else for(unsigned x=x0;x<=x1;x++) hud_touch_pixel(fb,x,y0,72);
}
static const uint8_t *hud_glyph(char c)
{
    static const char letters[]="SHAREPTIONL3CUDBK";
    static const uint8_t rows[][7]={
        {14,17,16,14,1,17,14},       /* S */
        {17,17,17,31,17,17,17},      /* H */
        {14,17,17,31,17,17,17},      /* A */
        {30,17,17,30,20,18,17},      /* R */
        {31,16,16,30,16,16,31},      /* E */
        {30,17,17,30,16,16,16},      /* P */
        {31,4,4,4,4,4,4},            /* T */
        {14,4,4,4,4,4,14},           /* I */
        {14,17,17,17,17,17,14},      /* O */
        {17,25,25,21,19,19,17},      /* N */
        {16,16,16,16,16,16,31},      /* L */
        {30,1,1,14,1,1,30},          /* 3 */
        {14,17,16,16,16,17,14},      /* C */
        {17,17,17,17,17,17,14},      /* U */
        {30,17,17,17,17,17,30},      /* D */
        {30,17,17,30,17,17,30},      /* B */
        {17,18,20,24,20,18,17}       /* K */
    };
    for(unsigned i=0;i<sizeof(letters)-1;i++) if(letters[i]==c) return rows[i];
    return NULL;
}
static void hud_touch_text(uint16_t *fb,const char *text,unsigned center,unsigned y,unsigned scale)
{
    unsigned n=(unsigned)strlen(text),step=6u*scale;
    unsigned left=center-(n*step-scale)/2u;
    for(unsigned i=0;i<n;i++) {
        const uint8_t *glyph=hud_glyph(text[i]); if(!glyph) continue;
        for(unsigned gy=0;gy<7;gy++) for(unsigned gx=0;gx<5;gx++)
            if(glyph[gy]&(1u<<(4u-gx)))
                for(unsigned sy=0;sy<scale;sy++) for(unsigned sx=0;sx<scale;sx++)
                    hud_touch_pixel(fb,left+i*step+gx*scale+sx,y+gy*scale+sy,176);
    }
}
static void draw_hud_touch_overlay(uint16_t *fb)
{
    hud_touch_line(fb,0,0,319,0); hud_touch_line(fb,0,79,319,79);
    hud_touch_line(fb,0,0,0,239); hud_touch_line(fb,106,0,106,239);
    hud_touch_line(fb,212,0,212,239); hud_touch_line(fb,319,0,319,239);
    hud_touch_line(fb,0,239,319,239);
    hud_touch_text(fb,"SHARE",53,38,1); hud_touch_text(fb,"PS",159,38,1);
    hud_touch_text(fb,"OPTION",266,38,1);
    hud_touch_line(fb,107,159,212,159);
    hud_touch_text(fb,"L3",53,157,1);
    hud_touch_text(fb,"PAD",159,116,1);
    hud_touch_text(fb,"CLICK",159,196,1);
    hud_touch_text(fb,"R3",266,157,1);
}
static void draw_touchpad_view(uint16_t *fb)
{
    hud_touch_line(fb,0,0,319,0); hud_touch_line(fb,0,0,0,239);
    hud_touch_line(fb,319,0,319,239); hud_touch_line(fb,0,239,319,239);
    hud_touch_line(fb,0,INPUT_3DS_TOUCHPAD_HEIGHT-1,319,INPUT_3DS_TOUCHPAD_HEIGHT-1);
    hud_touch_line(fb,INPUT_3DS_TOUCHPAD_BACK_X,INPUT_3DS_TOUCHPAD_HEIGHT,
        INPUT_3DS_TOUCHPAD_BACK_X,239);
    hud_touch_line(fb,INPUT_3DS_TOUCHPAD_EDGE_WIDTH,0,INPUT_3DS_TOUCHPAD_EDGE_WIDTH,199);
    hud_touch_line(fb,320-INPUT_3DS_TOUCHPAD_EDGE_WIDTH,0,320-INPUT_3DS_TOUCHPAD_EDGE_WIDTH,199);
    hud_touch_text(fb,"LEFT",20,88,1);
    hud_touch_text(fb,"CLICK",20,104,1);
    hud_touch_text(fb,"RIGHT",300,88,1);
    hud_touch_text(fb,"CLICK",300,104,1);
    hud_touch_text(fb,"TOUCHPAD",159,96,1);
    hud_touch_text(fb,"CLICK",119,216,1);
    hud_touch_text(fb,"BACK",279,216,1);
    touch_trail_draw(&touch_trail,fb,(uint32_t)chiaki_time_now_monotonic_ms());
}
static void draw_controller_view(void)
{
    uint16_t *fb=(uint16_t *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
    memset(fb,0,ZOOM_BOTTOM_PIXELS*sizeof(uint16_t));
    if(touch_capture.touchpad_mode) draw_touchpad_view(fb);
    else if(app_settings.touch_grid) draw_hud_touch_overlay(fb);
}
static void draw_zone(const Input3dsTouchZone *zone)
{
    unsigned left=zone->x0/8,right=(zone->x1-1)/8;
    unsigned top=zone->y0/8,bottom=(zone->y1-1)/8;
    for(unsigned row=top;row<=bottom;row++) {
        console_at(row,left);
        for(unsigned col=left;col<=right;col++)
            putchar(row==top || row==bottom ? '-' : col==left || col==right ? '|' : ' ');
    }
    unsigned length=(unsigned)strlen(zone->label);
    console_at((top+bottom)/2,left+(right-left+1-length)/2);
    fputs(zone->label,stdout);
}
/* Position printable characters explicitly: implicit console wrapping can
 * trigger graphics IPC during shutdown. Keep column 39 and last row safe. */
static void draw_stop_text(unsigned row,unsigned last_row,const char *text)
{
    char line[40];
    while(row<=last_row && text_wrap_line(&text,line,39)) {
        for(unsigned column=0;line[column];column++) {
            unsigned char c=(unsigned char)line[column];
            if(c<32 || c==127) continue;
            console_at(row,column); putchar(c);
        }
        row++;
    }
}
static void draw_status(void)
{
    draw_stop_text(13,16,status);
}

static void draw_stopping(void)
{

    /* consoleInit's returned handle is live; consoleGetDefault is a template.
     * Bottom remains single-buffered RGB565 after initialization. No console
     * reinitialization, framebuffer swap, cache flush or wait belongs here. */
    do { memset(bottom_console->frameBuffer,0,BOTTOM_FRAMEBUFFER_BYTES); } while(0);
    do { bottom_console->cursorX=bottom_console->cursorY=1; } while(0);
    do { bottom_console->prevCursorX=bottom_console->prevCursorY=1; } while(0);

    char line[80];
    do { snprintf(line,sizeof(line),"3DSRP  Stopping safely  #%lu",(unsigned long)attempt); } while(0);
    do { draw_stop_text(0,0,line); } while(0);
    /* Fixed zone coordinates end at column 38, row 27; labels contain only
     * printable text. They cannot wrap or reach the scrolling boundary. */
    for(unsigned index=0;index<INPUT_3DS_TOUCH_ZONE_COUNT;index++)
        do { draw_zone(&input_3ds_touch_zones[index]); } while(0);
    do { draw_stop_text(13,16,status); } while(0);

    do { draw_stop_text(28,28,"B cancel  START safe exit"); } while(0);

}

static bool save_zoom_settings(void)
{
    if(!zoom_settings_pending) return true;
    app_settings.reading_view=zoom_controls.reading;
    app_settings.hud_view=zoom_controls.saved;
    uint32_t revision=settings_writer_submit(settings_writer,&app_settings);
    if(!revision) {
        zoom_settings_changed_at=(uint32_t)chiaki_time_now_monotonic_ms();
        snprintf(recovery_notice,sizeof(recovery_notice),"Magnifier positions could not be saved.");
        return false;
    }
    zoom_save_revision=revision;
    zoom_settings_pending=false;
    return true;
}

static void poll_settings_saves(void)
{
    /* Normal gameplay has no settings work: avoid taking the writer mutex
     * on every UI iteration when no save completion is outstanding. */
    if(!menu_save_revision && !zoom_save_revision) return;
    SettingsWriteStatus saved=settings_writer_status(settings_writer);
    if(menu_save_revision && saved.completed>=menu_save_revision) {
        if(saved.persisted>=menu_save_revision) {
            app_settings.migration_pending=settings_draft.migration_pending=false;
            recovery_notice[0]='\0';
            settings_notice[0]='\0';
        } else {
            snprintf(settings_notice,sizeof(settings_notice),"Save failed; change is active until app exits. Change it again to retry.");
            if(menu_save_has_crops) {
                zoom_settings_pending=true;
                zoom_settings_changed_at=(uint32_t)chiaki_time_now_monotonic_ms();
            }
        }
        menu_save_revision=0;
    }
    if(zoom_save_revision && saved.completed>=zoom_save_revision) {
        if(saved.persisted>=zoom_save_revision) {
            app_settings.migration_pending=false;
            if(!strncmp(recovery_notice,"Magnifier positions",19)) recovery_notice[0]='\0';
        } else if(saved.requested==zoom_save_revision) {
            if(!zoom_settings_pending) zoom_settings_changed_at=(uint32_t)chiaki_time_now_monotonic_ms();
            zoom_settings_pending=true;
            snprintf(recovery_notice,sizeof(recovery_notice),"Magnifier positions could not be saved.");
        }
        zoom_save_revision=0;
    }
}

static void set_phase(Phase next)
{
    if(phase==UI_STREAM && next!=UI_STREAM) {
        usage_stats_end_session(&usage,(uint32_t)chiaki_time_now_monotonic_ms());
        (void)save_zoom_settings();
        do { zoom_controls_reset_session(&zoom_controls); } while(0);
        do { input_3ds_reset_capture(&touch_capture); } while(0);
    }
    if(next==UI_FLUSHING) {
        if(zoom_settings_pending && !menu_save_revision) (void)save_zoom_settings();
        settings_writer_stop(settings_writer);
    }

    phase=next;
    phase_since=(uint32_t)chiaki_time_now_monotonic_ms();
}

static void remember_failure(const char *message)
{
    if(!attempt_failure[0]) snprintf(attempt_failure,sizeof(attempt_failure),"%s",message);
}

static void remember_client_failure(RpState state)
{
    RpError error=rp_error(client);
    bool direct=!pairing_flow && consoles.count && console_address_is_public(consoles.records[consoles.selected].address);
    if(direct && (error==RP_ERROR_TIMEOUT || error==RP_ERROR_NETWORK))
        remember_failure("Internet session failed. Check address, forwarding and that console is awake.");
    else if(error==RP_ERROR_WAKE_TIMEOUT) remember_failure("Console did not wake. Check Rest Mode network settings.");
    else if(error==RP_ERROR_WAKE) remember_failure("Could not send the console wake request. Check Wi-Fi.");
    else if(error==RP_ERROR_WAKE_CREDENTIAL) remember_failure("Saved pairing cannot wake console. Turn it on manually.");
    else if(state==RP_STANDBY) remember_failure("Console is in standby.");
    else if(state==RP_WRONG_CONSOLE || error==RP_ERROR_IDENTITY)
        remember_failure("Console identity did not match.");
    else if(error==RP_ERROR_TIMEOUT && phase==UI_DISCOVERY)
        remember_failure("Discovery timed out.");
    else if(error==RP_ERROR_TIMEOUT) remember_failure("Session connection timed out.");
    else if(error==RP_ERROR_BUSY) remember_failure("Remote Play is already in use.");
    else if(error==RP_ERROR_NETWORK && phase==UI_DISCOVERY)
        remember_failure(pairing_flow ? "Discovery network error." : "Console connection failed. Check address and network.");
    else if(error==RP_ERROR_NETWORK) remember_failure("Console session connection failed.");
    else if(error==RP_ERROR_UNSUPPORTED) remember_failure("This build supports PS4 system software 8.0 or newer.");
    else if(pairing_flow && error==RP_ERROR_ACCOUNT)
        remember_failure("Console rejected the Account ID. Check it and use the ID for the console user you want.");
    else if(pairing_flow && error==RP_ERROR_CODE)
        remember_failure("Console rejected the code. Reopen Remote Play and enter a fresh 8-digit code.");
    else if(pairing_flow)
        remember_failure("Pairing failed. Check IP and Account ID, then reopen Remote Play for a fresh code.");
    else if(error==RP_ERROR_PROTOCOL && phase==UI_DISCOVERY)
        remember_failure("Discovery failed.");
    else if(error==RP_ERROR_PROTOCOL) remember_failure("Console session protocol failed.");
    else if(error==RP_ERROR_LOGIN) remember_failure("Console rejected the login passcode.");
    else remember_failure("Console session failed.");
}

static bool video_sample(uint8_t *buffer,size_t size,unsigned width,unsigned height,int32_t lost,void *user)
{
    return video_pipeline_push(user,buffer,size,width,height,lost);
}

static void request_stop(bool exiting,uint32_t reason)
{
    (void)reason;
    release_options_pulse();
    if(pairing_flow) { secret_clear(pairing_pin,sizeof(pairing_pin)); secret_clear(pairing_account,sizeof(pairing_account)); pairing_flow=false; pairing_wait_pin=false; }

    do { local_menu=MENU_NONE; } while(0);

    exit_requested |= exiting;
    if(phase==UI_STOPPING || phase==UI_FLUSHING) {

        return;
    }

    if(!live_active && (phase==UI_READY || phase==UI_DONE)) {
        do { set_phase(UI_FLUSHING); } while(0);

        return;
    }
    cleanup_started=false;
    video_stop_requested=false;

    /* Atomic admission close and decoder cancellation only: no network mutex
     * or wait is permitted on the UI stop path. */
    {

        do { video_pipeline_stop(video); } while(0);

        video_stop_requested=true;
        if(live_active) {

            do { cleanup_started=rp_request_stop(client); } while(0);

        }
    }

    do { set_phase(UI_STOPPING); } while(0);

}

/* Leave a live session through the same completed-disconnect state used by
 * the in-app Disconnect action. Only after that returns to UI_DONE do we
 * start the separate app shutdown phase. */
static void request_app_exit(uint32_t reason)
{
    if(exit_after_disconnect) return;
    if(live_active) {
        exit_after_disconnect=true;
        if(phase!=UI_STOPPING && phase!=UI_FLUSHING) request_stop(false,2);
    } else request_stop(true,reason);
}

static bool media_join(void *user)
{
    return video_pipeline_join(user);
}

static void start_attempt(void)
{
    usage_stats_begin_session(&usage);

    if(!rp_has_pair(client)) {
        local_menu=MENU_CONSOLES;
        menu_row=0;
        snprintf(status,sizeof(status),"No saved console. Choose Add console to register one.");
        return;
    }
    if(!rp_set_stream_settings(client,&app_settings)) {
        snprintf(status,sizeof(status),"Stream settings unavailable; try after stopping.");
        return;
    }
    attempt_failure[0]='\0';
    rest_mode_requested=false;
    rest_mode_queue_failed=false;
    rest_mode_action_pending=false;
    rest_mode_exit_after_action=false;
    video_stop_requested=false;
    ++attempt;
    do { input_3ds_reset_capture(&touch_capture); } while(0);
    controller_valid=false;

    attempt_since=(uint32_t)chiaki_time_now_monotonic_ms();
    attempt_failure[0]='\0';
    live_active=true;
    if(!video_pipeline_start(video)) {
        remember_failure("Video worker could not start.");
        snprintf(status,sizeof(status),"Video worker unavailable. START exits safely.");
        request_stop(false,4);
        return;
    }
    set_phase(UI_DISCOVERY);
    if(RP_ATTEMPT_MS)
        snprintf(status,sizeof(status),"Discovering selected console.\nSession limit: %u seconds.",RP_ATTEMPT_MS/1000);
    else
        snprintf(status,sizeof(status),"Discovering selected console.\nDiscovery waits up to 5 seconds.");
    if(!rp_start_attempt(client)) {
        remember_client_failure(rp_state(client));
        request_stop(false,4);
    }
}

static void menu_item(unsigned row,unsigned index,const char *label,unsigned selected)
{
    ui_skin_item(row,index==selected);
    console_at(row,0); printf("%c %.37s",index==selected ? '>' : ' ',label);
}
static bool settings_changed(void)
{
    return settings_draft.bottom_view!=app_settings.bottom_view ||
        settings_draft.onscreen_helpers!=app_settings.onscreen_helpers ||
        settings_draft.touch_grid!=app_settings.touch_grid ||
        settings_draft.hud_hold_buttons!=app_settings.hud_hold_buttons ||
        settings_draft.push_options_on_menu!=app_settings.push_options_on_menu;
}
static void open_settings(LocalMenu parent)
{
    settings_parent=parent; settings_draft=app_settings; settings_row=0; settings_notice[0]='\0';
    local_menu=MENU_SETTINGS;
}
static void save_settings_change(void)
{
    settings_draft.reading_view=app_settings.reading_view;
    settings_draft.hud_view=app_settings.hud_view;
    app_settings=settings_draft;
    controller_view=app_settings.bottom_view!=APP_BOTTOM_HUD;
    menu_save_has_crops=zoom_settings_pending || zoom_save_revision;
    menu_save_revision=settings_writer_submit(settings_writer,&app_settings);
    if(menu_save_revision) {
        zoom_settings_pending=false;
        settings_notice[0]='\0';
    } else snprintf(settings_notice,sizeof(settings_notice),"Could not queue save; change is active until app exits.");
}
static void open_help(LocalMenu parent)
{
    help_parent=parent; help_page=0; local_menu=MENU_HELP;
}
static void open_confirmation(bool exiting,LocalMenu parent)
{
    confirm_exit=exiting; confirm_parent=parent;
    local_menu=MENU_CONFIRM; menu_row=0; /* Cancel always starts focused. */
}
static bool text_entry(const char *hint,char *out,size_t size)
{
    SwkbdState keyboard; swkbdInit(&keyboard,SWKBD_TYPE_NORMAL,2,(int)size-1);
    swkbdSetHintText(&keyboard,hint);
    if(out[0]) swkbdSetInitialText(&keyboard,out);
    SwkbdButton button=swkbdInputText(&keyboard,out,(int)size);
    return button==SWKBD_BUTTON_CONFIRM && out[0];
}
static bool begin_add_console(void)
{
    usage_stats_begin_session(&usage);
    char account_text[32]={0}; struct in_addr parsed;
    if(!text_entry("Console IPv4 address",pairing_ip,sizeof(pairing_ip))||inet_pton(AF_INET,pairing_ip,&parsed)!=1) { snprintf(status,sizeof(status),"Canceled or invalid IPv4 address."); return false; }
    if(!text_entry("PSN account ID (Base64)",account_text,sizeof(account_text))||!cfg_decode_account(account_text,pairing_account)) { secret_clear(account_text,sizeof(account_text)); secret_clear(pairing_account,8); snprintf(status,sizeof(status),"Account ID was not accepted."); return false; }
    secret_clear(account_text,sizeof(account_text));
    if(!text_entry("8-digit Remote Play code",pairing_pin,sizeof(pairing_pin))||strlen(pairing_pin)!=8) { secret_clear(pairing_pin,sizeof(pairing_pin)); secret_clear(pairing_account,8); snprintf(status,sizeof(status),"Pairing canceled; open Remote Play for a fresh code."); return false; }
    for(unsigned i=0;i<8;i++) if(pairing_pin[i]<'0'||pairing_pin[i]>'9') { secret_clear(pairing_pin,sizeof(pairing_pin)); secret_clear(pairing_account,8); snprintf(status,sizeof(status),"Remote Play code must use 8 digits."); return false; }
    if(!rp_set_pair_target(client,pairing_ip,NULL)||!rp_discover(client)) { secret_clear(pairing_pin,sizeof(pairing_pin)); secret_clear(pairing_account,8); snprintf(status,sizeof(status),"Could not start console discovery."); return false; }
    attempt_failure[0]='\0'; attempt_since=(uint32_t)chiaki_time_now_monotonic_ms();
    live_active=true;
    pairing_flow=true; pairing_wait_pin=false; set_phase(UI_DISCOVERY); local_menu=MENU_NONE;
    snprintf(status,sizeof(status),"Finding console to pair..."); return true;
}
static bool backup_consoles(ConsoleStore *backup)
{
    if(console_store_clone(backup,&consoles)) return true;
    snprintf(console_notice,sizeof(console_notice),"Not enough memory to change addresses.");
    return false;
}
static void change_console_address(unsigned index,bool edit)
{
    char address[64]={0};
    console_notice[0]=0;
    if(edit) snprintf(address,sizeof(address),"%s",consoles.records[index].address);
    if(!text_entry(edit?"Edit console IPv4 address":"Additional console IPv4 address",address,sizeof(address))) return;
    ConsoleStore before={0};
    if(!backup_consoles(&before)) return;
    bool changed=edit?console_store_edit_address(&consoles,index,address):console_store_add_address(&consoles,index,address);
    if(changed && console_store_save(RP_CONSOLES_FILE,&consoles)) {
        if(!edit || index==consoles.selected) rp_load_record(client,&consoles.records[consoles.selected]);
        if(!edit) menu_row=consoles.selected;
        status[0]=0;
    } else {
        console_store_rollback(&consoles,&before);
        snprintf(console_notice,sizeof(console_notice),"Address not saved: invalid, duplicate, memory or SD error.");
        snprintf(status,sizeof(status),"%s",console_notice);
    }
    console_store_free(&before);
}
static void resume_game(void)
{
    if(app_settings.push_options_on_menu) {
        release_options_pulse();
        push_options_button();
    }
    local_menu=MENU_NONE;
    zoom_controls_rearm(&zoom_controls);
    input_3ds_reset_capture(&touch_capture);
}
static void update_local_menu(uint32_t pressed,const Input3dsSnapshot *snapshot)
{
    bool ready=phase==UI_READY || phase==UI_DONE;
    if(local_menu==MENU_USAGE) {
        if(pressed & (KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT)) usage_row^=1u;
        else if(pressed & (KEY_A|KEY_B|KEY_START)) {
            UsagePreferences next=usage_preferences;
            next.choice=(pressed & KEY_A) && usage_row==0 ? USAGE_ALLOWED : USAGE_DECLINED;
            if(next.choice==USAGE_DECLINED || usage_preferences.choice!=USAGE_ALLOWED)
                next.generation=(next.generation+1u)&0xffffffu;
            /* Withdrawal takes effect immediately, even if the SD card fails. */
            UsagePreferences off=usage_preferences;off.choice=USAGE_DECLINED;
            if(next.choice==USAGE_DECLINED)usage_stats_enable(&usage,&off);
            else usage_stats_hold(&usage,(uint32_t)chiaki_time_now_monotonic_ms());
            if(next.choice==USAGE_ALLOWED && !next.uuid[0] && !usage_random_uuid(next.uuid)) {
                snprintf(usage_notice,sizeof(usage_notice),"Could not enable statistics. Try again.");return;
            }
            if(!usage_preferences_save(USAGE_FILE,&next)) {
                usage_preferences.choice=USAGE_DECLINED;
                snprintf(usage_notice,sizeof(usage_notice),"Not saved; OFF for this app launch.");return;
            }
            usage_preferences=next;usage_stats_enable(&usage,&usage_preferences);
            local_menu=usage_parent;return;
        }
        return;
    }
    if(local_menu==MENU_HUD_HOLD) {
        if(pressed & (KEY_B|KEY_START)) { local_menu=MENU_SETTINGS; return; }
        if(pressed & KEY_UP) hud_hold_row=(hud_hold_row+12)%13;
        else if(pressed & KEY_DOWN) hud_hold_row=(hud_hold_row+1)%13;
        else if(pressed & (KEY_A|KEY_LEFT|KEY_RIGHT)) {
            if(hud_hold_row==12) { if(pressed & KEY_A) local_menu=MENU_SETTINGS; }
            else {
                if(settings_draft.hud_hold_buttons==hud_hold_bits[hud_hold_row]) {
                    snprintf(settings_notice,sizeof(settings_notice),"Keep one button ON; use Blank instead.");
                } else {
                    settings_draft.hud_hold_buttons^=hud_hold_bits[hud_hold_row];
                    save_settings_change();
                }
            }
        }
        return;
    }
    if(local_menu==MENU_SETTINGS) {
        unsigned count=SETTINGS_COUNT;
        if(pressed & (KEY_B|KEY_START)) {
            local_menu=settings_parent;
            menu_row=0; return;
        }
        if(pressed & KEY_UP) settings_row=(settings_row+count-1)%count;
        else if(pressed & KEY_DOWN) settings_row=(settings_row+1)%count;
        else if(settings_row==SETTINGS_USAGE && (pressed & (KEY_A|KEY_LEFT|KEY_RIGHT))) {
            usage_parent=MENU_SETTINGS;usage_row=0;usage_notice[0]=0;local_menu=MENU_USAGE;
        }
        else if(settings_row==SETTINGS_SIXAXIS && (pressed & (KEY_A|KEY_LEFT|KEY_RIGHT))) {
            sixaxis_enabled=!sixaxis_enabled;
            motion_3ds_poll(sixaxis_enabled && phase==UI_STREAM && rp_state(client)==RP_CONNECTED);
        }
        else if(pressed & (KEY_LEFT|KEY_RIGHT)) {
            if(app_settings_cycle(&settings_draft,settings_row,(pressed & KEY_LEFT) ? -1 : 1)) {
                settings_notice[0]='\0';
                save_settings_change();
                if(settings_row==APP_SETTING_BOTTOM_VIEW && settings_draft.bottom_view==APP_BOTTOM_HOLD_HUD) {
                    hud_hold_row=0; local_menu=MENU_HUD_HOLD;
                }
            }
        } else if(pressed & KEY_A) {
            if(settings_row==SETTINGS_BACK) { local_menu=settings_parent; menu_row=0; }
            else if(settings_row==APP_SETTING_BOTTOM_VIEW && settings_draft.bottom_view==APP_BOTTOM_HOLD_HUD) {
                hud_hold_row=0; local_menu=MENU_HUD_HOLD;
            }
        }
        return;
    }
    if(local_menu==MENU_HELP) {
        if(pressed & (KEY_B|KEY_START)) {
            local_menu=help_parent;
            /* Returning to a magnifier must not treat Back as a fresh close. */
            zoom_controls.previous_held=snapshot->held;
            zoom_controls.previous_touch=snapshot->touch_down;
            zoom_controls.revision++;
        } else if(pressed & (KEY_LEFT|KEY_L)) help_page=(help_page+HELP_PAGE_COUNT-1)%HELP_PAGE_COUNT;
        else if(pressed & (KEY_RIGHT|KEY_R|KEY_A)) help_page=(help_page+1)%HELP_PAGE_COUNT;
        return;
    }
    if(local_menu==MENU_CONFIRM) {
        unsigned option_count=live_active && phase==UI_STREAM ? 3u : 2u;
        if(pressed & (KEY_B|KEY_START)) { local_menu=confirm_parent; menu_row=0; }
        else if(pressed & (KEY_UP|KEY_LEFT)) menu_row=(menu_row+option_count-1)%option_count;
        else if(pressed & (KEY_DOWN|KEY_RIGHT)) menu_row=(menu_row+1)%option_count;
        else if(pressed & KEY_A) {
            if(!menu_row) { local_menu=confirm_parent; menu_row=0; }
            else {
                bool has_sleep_choice=option_count==3;
                bool sleep_console=has_sleep_choice && menu_row==2;
                if(sleep_console) {
                    rest_mode_requested=true;
                    rest_mode_queue_failed=!rp_goto_bed(client);

                    if(!rest_mode_queue_failed) {
                        /* Do not start session teardown until the control
                         * worker reports the actual transport send result. */
                        rest_mode_action_pending=true;
                        rest_mode_exit_after_action=confirm_exit;
                        local_menu=MENU_NONE;
                        return;
                    }
                }
                if(confirm_exit) request_app_exit(1);
                else request_stop(false,2);
            }
        }
        return;
    }
    if(local_menu==MENU_CONSOLES) {
        if((pressed & (KEY_X|KEY_Y)) && menu_row<consoles.count) {
            change_console_address(menu_row,(pressed & KEY_Y)!=0); return;
        }
        if((pressed & (KEY_X|KEY_Y)) && menu_row>=consoles.count) {
            snprintf(console_notice,sizeof(console_notice),"Highlight an address first."); return;
        }
        if(pressed & (KEY_B|KEY_START)) local_menu=MENU_NONE;
        else {
            unsigned count=consoles.count+3;
            if(pressed & KEY_UP) menu_row=(menu_row+count-1)%count;
            else if(pressed & KEY_DOWN) menu_row=(menu_row+1)%count;
            else if(pressed & KEY_A) {
                if(menu_row<consoles.count) {
                    ConsoleStore before={0};
                    if(!backup_consoles(&before)) return;
                    consoles.selected=menu_row;
                    if(console_store_save(RP_CONSOLES_FILE,&consoles) && rp_load_record(client,&consoles.records[menu_row]))
                        status[0]='\0';
                    else { console_store_rollback(&consoles,&before); snprintf(console_notice,sizeof(console_notice),"Could not select or save this address."); }
                    console_store_free(&before);
                } else if(menu_row==consoles.count) (void)begin_add_console();
                else if(menu_row==consoles.count+1 && consoles.count) { console_remove_index=consoles.selected; local_menu=MENU_CONSOLE_REMOVE_CONFIRM; menu_row=0; }
                else local_menu=MENU_NONE;
            }
        }
        return;
    }
    if(local_menu==MENU_CONSOLE_REMOVE_CONFIRM) {
        if(pressed & (KEY_B|KEY_START)) { local_menu=MENU_CONSOLES; menu_row=0; }
        else if(pressed & (KEY_UP|KEY_DOWN)) menu_row=(menu_row+1)%2;
        else if(pressed & KEY_A) {
            if(menu_row==0) { local_menu=MENU_CONSOLES; menu_row=0; return; }
            unsigned removed=console_remove_index; ConsoleStore before={0};
            if(!backup_consoles(&before)) return;
            if(console_store_remove(&consoles,removed) && console_store_save(RP_CONSOLES_FILE,&consoles)) {
                if(consoles.count) rp_load_record(client,&consoles.records[consoles.selected]);
                else rp_clear_active_record(client);
                status[0]=0; console_notice[0]=0;
            } else { console_store_rollback(&consoles,&before); snprintf(console_notice,sizeof(console_notice),"Could not remove saved address."); }
            console_store_free(&before); local_menu=MENU_CONSOLES; menu_row=0;
        }
        return;
    }
    if(local_menu==MENU_SESSION) {
        if(pressed & (KEY_B|KEY_START)) { resume_game(); return; }
        if(pressed & KEY_UP) menu_row=(menu_row+SESSION_ITEM_COUNT-1)%SESSION_ITEM_COUNT;
        else if(pressed & KEY_DOWN) menu_row=(menu_row+1)%SESSION_ITEM_COUNT;
        else if(pressed & KEY_A) {
            switch(menu_row) {
            case 0: resume_game(); break;
            case 1: open_settings(MENU_SESSION); break;
            case 2: open_help(MENU_SESSION); break;
            case 3: open_confirmation(false,MENU_SESSION); break;
            }
        }
        return;
    }
    if(ready) {
        if(pressed & KEY_START) open_confirmation(true,MENU_NONE);
        else if(pressed & KEY_Y) open_settings(MENU_NONE);
        else if(pressed & KEY_UP) ready_row=(ready_row+4)%5;
        else if(pressed & KEY_DOWN) ready_row=(ready_row+1)%5;
        else if(pressed & KEY_A) {
            switch(ready_row) {
            case 0: start_attempt(); break;
            case 1: open_settings(MENU_NONE); break;
            case 2: local_menu=MENU_CONSOLES; menu_row=0; console_notice[0]=0; break;
            case 3: open_help(MENU_NONE); break;
            case 4: open_confirmation(true,MENU_NONE); break;
            }
        }
    }
}
static bool draw_local_menu(void)
{
    static const char *const ready_items[]={"Connect","Settings","Consoles","Help","Exit"};
    static const char *const session_items[SESSION_ITEM_COUNT]={"Resume","Settings","Help / Controls","Disconnect"};
    if(local_menu==MENU_USAGE) {
        console_at(0,0);printf("Usage statistics");
        console_at(5,0);printf("Allow AuroraStudios84 to collect the");
        console_at(7,0);printf("streaming duration from this homebrew?");
        console_at(11,0);printf("Only for usage statistics.");
        menu_item(15,0,"Allow",usage_row);
        menu_item(17,1,"No Thanks",usage_row);
        console_at(29,0);printf("%.39s",usage_notice);return true;
    }
    if(local_menu==MENU_HUD_HOLD) {
        console_at(0,0); printf("Hold for HUD");
        console_at(2,0); printf("Show the magnified HUD while held.");
        char line[64];
        for(unsigned i=0;i<12;i++) {
            snprintf(line,sizeof(line),"%s: %s",hud_hold_labels[i],
                settings_draft.hud_hold_buttons & hud_hold_bits[i] ? "ON" : "OFF");
            menu_item(4+i,i,line,hud_hold_row);
        }
        menu_item(17,12,"Back",hud_hold_row);
        console_at(19,0); printf("Adjust crop: SELECT twice during play.");
        console_at(20,0); printf("Circle Pad moves; L/R resizes the crop.");
        console_at(23,0); printf("Buttons still work in-game.");
        console_at(25,0); printf("Up/Down select; A/Left/Right edit");
        console_at(27,0); printf("B Back");
        console_at(29,0); printf("%.39s",settings_notice);
        return true;
    }
    if(local_menu==MENU_SETTINGS) {
        console_at(0,0); printf("Settings%s",settings_changed() ? " *" : "");
        char line[64];
        snprintf(line,sizeof(line),"Touch grid: %s",settings_draft.touch_grid ? "ON" : "OFF"); menu_item(3,APP_SETTING_TOUCH_GRID,line,settings_row);
        snprintf(line,sizeof(line),"Bottom view: %s",app_settings_bottom_view_label(&settings_draft)); menu_item(5,APP_SETTING_BOTTOM_VIEW,line,settings_row);
        snprintf(line,sizeof(line),"Push Options on entering menu: %s",settings_draft.push_options_on_menu ? "ON" : "OFF"); menu_item(7,APP_SETTING_PUSH_OPTIONS,line,settings_row);
        snprintf(line,sizeof(line),"Sixaxis: %s",sixaxis_enabled ? "ON" : "OFF"); menu_item(9,SETTINGS_SIXAXIS,line,settings_row);
        snprintf(line,sizeof(line),"Usage statistics: %s",usage.enabled ? "ON" : "OFF");menu_item(11,SETTINGS_USAGE,line,settings_row);
        menu_item(13,SETTINGS_BACK,"Back",settings_row);
        console_at(23,0); printf("Hold for HUD opens button settings.");
        console_at(25,0); printf("Up/Down select; Left/Right edit");
        console_at(27,0); printf("A Open / toggle  B Back");
        console_at(29,0); printf("%.39s",settings_notice);
        return true;
    }
    if(local_menu==MENU_HELP) {
        console_at(0,0); printf("%.39s",help_page_titles[help_page]);
        console_at(1,0); printf("Help %u / %u",help_page+1,HELP_PAGE_COUNT);
        for(unsigned i=0;i<HELP_PAGE_LINES;i++) if(help_pages[help_page][i]) {
            console_at(HELP_LINE_ROW(i),0); printf("%.39s",help_pages[help_page][i]);
        }
        console_at(27,0); printf("Left/Right or L/R: page   A: next");
        console_at(28,0); printf("B: Back"); return true;
    }
    if(local_menu==MENU_CONFIRM) {
        bool has_sleep_choice=live_active && phase==UI_STREAM;
        console_at(0,0); printf("%s?",confirm_exit ? "Exit application" : "Disconnect from console");
        if(has_sleep_choice) {
            console_at(3,0); printf("Choose what to do with the console.");
            console_at(5,0); printf("Rest Mode is not a full power off.");
        } else {
            console_at(3,0); printf("%s",confirm_exit ? "Close Remote Play and leave this app." : "End this session and return to Ready.");
        }
        menu_item(9,0,"Cancel",menu_row);
        if(has_sleep_choice) {
            menu_item(11,1,confirm_exit ? "Keep console awake + Exit" : "Keep console awake + Disconnect",menu_row);
            menu_item(13,2,confirm_exit ? "Rest Mode + Exit" : "Rest Mode + Disconnect",menu_row);
        } else menu_item(11,1,confirm_exit ? "Exit" : "Disconnect",menu_row);
        console_at(27,0); printf("Up/Down select  A confirm  B Back"); return true;
    }
    if(local_menu==MENU_CONSOLE_REMOVE_CONFIRM) {
        console_at(0,0); printf("Remove address?");
        console_at(3,0); printf("%-.39s",consoles.records[console_remove_index].nickname);
        console_at(5,0); printf("%.15s",consoles.records[console_remove_index].address);
        console_at(7,0); printf("Other addresses are kept.");
        menu_item(9,0,"Cancel",menu_row); menu_item(11,1,"Remove address",menu_row);
        console_at(27,0); printf("Up/Down select  A confirm  B Back"); return true;
    }
    if(local_menu==MENU_CONSOLES) {
        console_at(0,0); printf("Consoles");
        if(consoles.count<=8) console_scroll=0;
        else if(console_scroll>consoles.count-8) console_scroll=consoles.count-8;
        if(menu_row<consoles.count) {
            if(menu_row<console_scroll) console_scroll=menu_row;
            else if(menu_row-console_scroll>=8) console_scroll=menu_row-7;
        }
        for(unsigned row=0;row<8 && row+console_scroll<consoles.count;row++) {
            unsigned i=row+console_scroll;
            char line[64]; snprintf(line,sizeof(line),"%s%.10s %s %.15s",i==consoles.selected?"* ":"  ",consoles.records[i].nickname,console_address_is_public(consoles.records[i].address)?"Internet":"Home",consoles.records[i].address);
            menu_item(3+row,i,line,menu_row);
        }
        if(consoles.count>8) { console_at(11,0); printf("Up/Down scrolls addresses"); }
        menu_item(13,consoles.count,"Add console",menu_row);
        menu_item(15,consoles.count+1,"Remove address",menu_row);
        menu_item(17,consoles.count+2,"Back",menu_row);
        console_at(19,0); printf("* = Primary address used by Connect");
        if(menu_row<consoles.count) {
            console_at(22,0); printf("%s: %.31s",consoles.records[menu_row].target==CHIAKI_TARGET_PS4_10?"PS4":"PS5",consoles.records[menu_row].nickname);
            console_at(24,0); printf("X Add address   Y Edit IP");
        } else if(menu_row==consoles.count+1 && consoles.count) {
            console_at(22,0); printf("Remove: %.15s",consoles.records[consoles.selected].address);
        }
        if(!consoles.count) { console_at(23,0); printf("No saved console. Choose Add console."); }
        draw_stop_text(25,26,console_notice);
        console_at(27,0); printf("%s",menu_row<consoles.count ?
            "Up/Down select  A set primary  B Back" : "Up/Down select  A open  B Back"); return true;
    }
    if(local_menu==MENU_SESSION) {
        console_at(0,0); printf("Session");
        if(app_settings.push_options_on_menu) {
            console_at(2,0); printf("Options sent on entry and resume.");
            console_at(3,0); printf("Game response depends on the title.");
        }
        for(unsigned i=0;i<SESSION_ITEM_COUNT;i++) {
            char line[64];
            snprintf(line,sizeof(line),"%s",session_items[i]);
            menu_item(5+i*2,i,line,menu_row);
        }
        console_at(20,0); printf("Local menu: console controls released.");
        console_at(22,0); printf("Release controls before resuming.");
        console_at(27,0); printf("Up/Down select  A open  B Resume"); return true;
    }
    if(phase==UI_READY || phase==UI_DONE) {
        console_at(0,0); printf("3DSRP - %s",phase==UI_DONE ? "Disconnected" : "Ready");
        for(unsigned i=0;i<5;i++) menu_item(3+i*2,i,ready_items[i],ready_row);
        draw_stop_text(16,19,status);
        console_at(20,0); printf("%.39s",recovery_notice);
        if(consoles.count) {
            console_at(22,0); printf("Primary console:");
            console_at(23,0); printf("%.39s",consoles.records[consoles.selected].nickname);
            console_at(24,0); printf("%s %.15s",console_address_is_public(consoles.records[consoles.selected].address)?"Internet":"Home",consoles.records[consoles.selected].address);
        }
        console_at(26,0); printf("Up/Down select  A open  Y Settings");
        console_at(28,0); printf("START: Exit confirmation"); return true;
    }
    if(phase==UI_STREAM && zoom_controls.mode==ZOOM_DIRTY) {
        console_at(0,0); printf("Unsaved HUD crop");
        console_at(3,0); printf("Apply crop for this app run?");
        menu_item(7,0,"Apply",zoom_controls.dirty_row);
        menu_item(9,1,"Discard",zoom_controls.dirty_row);
        menu_item(11,2,"Continue editing",zoom_controls.dirty_row);
        console_at(27,0); printf("Up/Down select  A choose  B Continue"); return true;
    }
    if(phase==UI_STREAM && controller_view && !zoom_controls_menu(&zoom_controls)) {
        draw_controller_view(); return true;
    }
    return false;
}

static void draw(void)
{

    static const char *const names[]={"Ready","Discovering","Connecting","Connected","Stopping safely","Disconnected","Saving settings"};
    if(phase==UI_STOPPING) {
        do { draw_stopping(); } while(0);

        return;
    }
    /* All bottom-screen UI is drawn into the current back buffer. Start each
     * console redraw from a clean frame so alternating buffers cannot reveal
     * stale text or a previous screen. */
    do { consoleClear(); } while(0);
    bool skin_visible=phase!=UI_STREAM || local_menu!=MENU_NONE || zoom_controls.mode==ZOOM_DIRTY;
    if(skin_visible) ui_skin_begin(
        local_menu==MENU_HELP ? UI_SKIN_HELP :
        local_menu==MENU_USAGE ? UI_SKIN_PRIVACY :
        local_menu==MENU_CONSOLES ? UI_SKIN_CONSOLES :
        local_menu==MENU_HUD_HOLD ? UI_SKIN_DENSE :
        local_menu==MENU_NONE && (phase==UI_READY || phase==UI_DONE) ? UI_SKIN_READY : UI_SKIN_MENU,
        consoles.count ? consoles.records[consoles.selected].nickname : NULL);
    if(draw_local_menu()) {
        if(skin_visible) ui_skin_end((uint16_t *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL));
        if(phase==UI_READY || phase==UI_DONE) {
            const uint16_t *brand=ui_skin_brand();
#ifdef N3DS_GPU_BILINEAR
            (void)video_pipeline_present_ui(video,brand);
#else
            uint16_t *top=(uint16_t *)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
            for(unsigned y=0;y<240;y++) for(unsigned x=0;x<400;x++) top[x*240+239-y]=brand[y*400+x];
            top_flush_pending=true;
#endif
        }

        return;
    }
    console_at(0,0); printf("3DSRP  %s  #%lu",(phase==UI_DISCOVERY && rp_state(client)==RP_WAKING) ? "Waking console" : names[phase],(unsigned long)attempt);
    draw_status();
    if(phase==UI_STREAM) {
        console_at(26,0); printf("Shoulders: L/R=L2/R2  ZL/ZR=L1/R1");
    }
    console_at(28,0);
    if(phase==UI_READY || phase==UI_DONE) printf("A open  Y Settings  START Exit menu");
    else if(phase==UI_STREAM) printf("SELECT Magnifier  START Session");
    else printf("B cancel  START safe exit");
    if(skin_visible) ui_skin_end((uint16_t *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL));

}

static void draw_zoom_legend(void)
{
    /* The requested setting may differ from the negotiated, displayed stream.
     * Keep this caption visible even with local menu/help closed. Row 29 is
     * separate from the mode badge; avoid the final column to prevent scroll. */
    if(!app_settings.onscreen_helpers) return;

    VideoStats stats=video_pipeline_stats(video);

    char stream_label[64];
    if(stats.presented && stats.width && stats.height)
        snprintf(stream_label,sizeof(stream_label),"Stream: %up (%ux%u)",
            stats.height,stats.width,stats.height);
    else snprintf(stream_label,sizeof(stream_label),"Stream: waiting for picture");
    console_at(29,0); printf("%-39.39s",stream_label);
    if(!zoom_controls_menu(&zoom_controls)) {
        console_at(0,0); printf("%-39s","HUD view touch controls active");
        console_at(1,0); printf("%-39s","SELECT Magnifier  START Session");
        return;
    }
    ZoomPresentation p=zoom_controls_presentation(&zoom_controls);
    console_at(0,0); printf("%-39s",zoom_controls.mode==ZOOM_READING ? "Reading (local controls)" : "HUD setup (local controls)");
    char line[80];
    snprintf(line,sizeof(line),"Zoom %u.%ux Pad pan L/R zoom Y centre",p.view.zoom_q/4,(p.view.zoom_q%4)*5/2);
    console_at(1,0); printf("%-39.39s",line);
    console_at(2,0); printf("%-39s",zoom_controls.mode==ZOOM_READING ?
        "SELECT HUD; B game controls" : "SELECT game controls; B closes");
    console_at(3,0); printf("%-39s","Game controls released during setup.");
    console_at(5,0); printf("%-39s","[ Help / Controls ]    START Session");
}

int main(void)
{
    zoom_controls_init(&zoom_controls);
#ifdef N3DS_GPU_BILINEAR
    /* Citro2D links only top and transfers RGB8. Bottom console/crop retains
     * its existing RGB565 format and independent manual buffer swaps. */
    gfxInit(GSP_BGR8_OES,GSP_BGR8_OES,false);
    gfxSetDoubleBuffering(GFX_TOP,true);
    for(unsigned buffer=0;buffer<2;buffer++) {
        void *fb=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
        memset(fb,0,TOP_FRAMEBUFFER_BYTES);
        if(R_SUCCEEDED(GSPGPU_FlushDataCache(fb,TOP_FRAMEBUFFER_BYTES)))
            gfxScreenSwapBuffers(GFX_TOP,false);
        gspWaitForVBlank();
    }
#else
    gfxInit(GSP_RGB565_OES,GSP_BGR8_OES,false);
    gfxSetDoubleBuffering(GFX_TOP,false);
    do {

        void *clear_framebuffer=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);

        do { memset(clear_framebuffer,0,VIDEO_SCREEN_PIXELS*sizeof(uint16_t)); } while(0);
    } while(0);
    top_flush_pending=true;
#endif
    bottom_console=consoleInit(GFX_BOTTOM,NULL);
    /* consoleInit selects a single live framebuffer. Re-enable double
     * buffering for the streamed bottom-screen image and draw into the
     * framebuffer that is not currently being scanned out. */
    gfxSetDoubleBuffering(GFX_BOTTOM,true);
    void *initial_bottom_framebuffer=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
    memset(initial_bottom_framebuffer,0,BOTTOM_FRAMEBUFFER_BYTES);
    Result initial_bottom_flush=GSPGPU_FlushDataCache(initial_bottom_framebuffer,BOTTOM_FRAMEBUFFER_BYTES);
    if(R_SUCCEEDED(initial_bottom_flush)) {
        gfxScreenSwapBuffers(GFX_BOTTOM,false);
        gspWaitForVBlank();
    }
    bottom_console->frameBuffer=(u16 *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
    bottom_flush_pending=R_FAILED(initial_bottom_flush);
    osSetSpeedupEnable(true);
    mkdir("sdmc:/3ds",0777);
    mkdir(RP_ROOT,0777);
    void *socket_buffer=memalign(0x1000,0x100000);
    if(!socket_buffer || R_FAILED(socInit(socket_buffer,0x100000))) {
        printf("Socket service unavailable. START exits.\n");
        GSPGPU_FlushDataCache(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL),BOTTOM_FRAMEBUFFER_BYTES);
        gfxScreenSwapBuffers(GFX_BOTTOM,false);
        bottom_console->frameBuffer=(u16 *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
        while(aptMainLoop()) {
            hidScanInput(); if(hidKeysDown() & KEY_START) break; do { gspWaitForVBlank(); } while(0);
        }
        do { free(socket_buffer); } while(0); do { gfxExit(); } while(0); return 1;
    }
    usage_stats_init(&usage,&usage_http_3ds);
    usage_stats_set_journal(&usage,USAGE_PENDING_FILE);
    (void)usage_preferences_load(USAGE_FILE,&usage_preferences);
    usage_stats_enable(&usage,&usage_preferences);
    if(usage_preferences.choice==USAGE_UNASKED) {
        local_menu=MENU_USAGE;usage_parent=MENU_NONE;usage_row=0;
    }
    StoreResult settings_loaded=app_settings_load(RP_SETTINGS_FILE,&app_settings);
    if(settings_loaded==STORE_INVALID)
        snprintf(recovery_notice,sizeof(recovery_notice),"Settings damaged; defaults restored.");
    else if(settings_loaded==STORE_IO)
        snprintf(recovery_notice,sizeof(recovery_notice),"Settings unreadable; using defaults.");
    zoom_controls_set_views(&zoom_controls,&app_settings.reading_view,&app_settings.hud_view);

    settings_writer=settings_writer_create(RP_SETTINGS_FILE);
    if(!settings_writer)
        snprintf(recovery_notice,sizeof(recovery_notice),"Settings writer unavailable; changes cannot be saved.");
    if(settings_loaded==STORE_OK && app_settings.migration_pending) {
        zoom_settings_pending=true;
        zoom_settings_changed_at=(uint32_t)chiaki_time_now_monotonic_ms();
        (void)save_zoom_settings();
    }
    client=rp_create();
    video=video_pipeline_create();
    controller_view=app_settings.bottom_view!=APP_BOTTOM_HUD;
    if(!client || !video) {
        do { rp_destroy(client); } while(0); do { video_pipeline_destroy(video); } while(0);
        settings_writer_stop(settings_writer);
        while(!settings_writer_join(settings_writer)) { svcSleepThread(16666667); }
        settings_writer_destroy(settings_writer);
        do { socExit(); } while(0); do { free(socket_buffer); } while(0); do { gfxExit(); } while(0); return 1;
    }
    StoreResult loaded=console_store_load(RP_CONSOLES_FILE,&consoles);
    if(loaded==STORE_OK && consoles.count) rp_load_record(client,&consoles.records[consoles.selected]);
    else if(loaded==STORE_MISSING) memset(&consoles,0,sizeof(consoles));
    else if(loaded!=STORE_OK) snprintf(status,sizeof(status),"Console database unreadable; saved pairings retained.");

    if(loaded==STORE_OK && consoles.count)
        status[0]='\0';
    else if(loaded==STORE_MISSING)
        snprintf(status,sizeof(status),"No saved console yet. Open Consoles to add one.");
    if(rp_audio_stats(client).failed) {
        size_t used=strlen(status);
        snprintf(status+used,sizeof(status)-used,"\nAudio unavailable (allocation).");
    }
    rp_set_video_callback(client,video_sample,video);
    rp_set_media_join_callback(client,media_join,video);
    phase_since=(uint32_t)chiaki_time_now_monotonic_ms();
    aptHook(&usage_apt_hook,usage_apt_event,NULL);
    unsigned redraw=0;
    uint32_t waiting_revision=UINT32_MAX;
    bool exit_loop=false;
    while(!exit_loop) {

        bool applet_active=aptMainLoop();

        if(!applet_active) request_app_exit(5);

        hidScanInput();
        uint32_t pressed=hidKeysDown(),held=hidKeysHeld();

        poll_settings_saves();
        if(rest_mode_action_pending) {
            int send_result=rp_goto_bed_result(client);
            if(send_result) {
                rest_mode_action_pending=false;
                rest_mode_queue_failed=send_result<0;
                if(rest_mode_exit_after_action) request_app_exit(1);
                else request_stop(false,2);
            }
        }
        if(exit_after_disconnect && phase==UI_DONE) {
            exit_after_disconnect=false;
            request_stop(true,1);
        }
        ZoomControlResult zoom_result={0};
        bool previous_touchpad_mode=touch_capture.touchpad_mode;
        Input3dsSnapshot snapshot={0};
        bool connected=phase==UI_STREAM && rp_state(client)==RP_CONNECTED;
        motion_3ds_poll(connected && sixaxis_enabled);
        if(connected) {
            snapshot=sample_input(held);
            if(local_menu==MENU_NONE) {
                VideoStats stats=video_pipeline_stats(video);
                zoom_result=zoom_controls_update(&zoom_controls,&snapshot,(uint32_t)chiaki_time_now_monotonic_ms(),stats.width,stats.height);
                if(zoom_result.saved) {
                    app_settings.reading_view=zoom_controls.reading;
                    app_settings.hud_view=zoom_controls.saved;
                    zoom_settings_pending=true;
                    zoom_settings_changed_at=(uint32_t)chiaki_time_now_monotonic_ms();
                }
                if(zoom_settings_pending && zoom_result.mode_changed)
                    (void)save_zoom_settings();
                if(zoom_result.reset_touch) input_3ds_reset_capture(&touch_capture);

                if(zoom_result.open_session) {
                    local_menu=MENU_SESSION; menu_row=0;
                    if(app_settings.push_options_on_menu) push_options_button();
                }
                else if(zoom_result.open_help) open_help(MENU_NONE);
                /* Entry presses belong to the screen just left. */
                if(zoom_result.open_session || zoom_result.open_help) pressed=0;
            }
            if((local_menu!=MENU_NONE || !zoom_result.forward) && !options_pulse_active) {
                Input3dsSnapshot neutral={0}; submit_controller(&neutral);
            }
        }
        if(phase!=UI_STOPPING && phase!=UI_FLUSHING) {
            if(local_menu!=MENU_NONE || phase==UI_READY || phase==UI_DONE)
                update_local_menu(pressed,&snapshot);
            else if(!connected && (pressed & KEY_START)) request_app_exit(1);
            else if(!connected && (pressed & KEY_B)) request_stop(false,2);
        } else if(pressed & KEY_START) request_app_exit(1);
        if(connected && phase==UI_STREAM && local_menu==MENU_NONE && zoom_result.forward && !options_pulse_active) {
            submit_controller(&snapshot);
        }
        bool trail_visible=connected && phase==UI_STREAM && local_menu==MENU_NONE &&
            zoom_result.forward && touch_capture.touchpad_mode;
        bool trail_dirty=touch_trail_update(&touch_trail,trail_visible,
            trail_visible && snapshot.touch_down && touch_capture.surface,
            snapshot.touch_x,snapshot.touch_y,(uint32_t)chiaki_time_now_monotonic_ms());
        bool next_controller_view=touch_capture.touchpad_mode || !app_settings_show_hud(&app_settings,snapshot.held,
            connected && local_menu==MENU_NONE && zoom_result.forward);
        bool touchpad_view_changed=previous_touchpad_mode!=touch_capture.touchpad_mode;
        bool bottom_view_changed=controller_view!=next_controller_view || touchpad_view_changed;
        controller_view=next_controller_view;
        /* Also redraw retained video if the stream has no new decoded frame. */
        if(bottom_view_changed) {
            zoom_controls.revision++;

        }

        uint32_t now=(uint32_t)chiaki_time_now_monotonic_ms();
        if(options_pulse_active && (uint32_t)(now-options_pulse_started)>=120u) {
            release_options_pulse();
        }
        if(phase!=UI_FLUSHING && !menu_save_revision && zoom_settings_pending &&
           (uint32_t)(now-zoom_settings_changed_at)>=750u)
            (void)save_zoom_settings();
        if(phase==UI_DISCOVERY || phase==UI_CONNECTING || phase==UI_STREAM) {
            RpState state=rp_state(client);
            if(pairing_flow && phase==UI_DISCOVERY && state==RP_FOUND && !pairing_wait_pin) {
                bool started=rp_pair(client,pairing_account,pairing_pin);
                secret_clear(pairing_pin,sizeof(pairing_pin)); pairing_wait_pin=started;
                if(!started) { secret_clear(pairing_account,8); pairing_flow=false; remember_failure("Could not start console pairing."); request_stop(false,4); }
                else snprintf(status,sizeof(status),"Pairing with console. Keep Remote Play open.");
            }
            if(pairing_flow && pairing_wait_pin && state==RP_PAIRED) {
                PairRecord candidate={0};
                ConsoleStore before={0};
                bool backed_up=backup_consoles(&before);
                bool candidate_ok=rp_candidate_record(client,&candidate);
                candidate.direct=console_address_is_public(candidate.address);
                bool ok=backed_up && candidate_ok && console_store_add(&consoles,&candidate) &&
                    console_store_save(RP_CONSOLES_FILE,&consoles) && rp_load_record(client,&consoles.records[consoles.selected]);
                if(!ok && backed_up) console_store_rollback(&consoles,&before);
                console_store_free(&before);
                secret_clear(&candidate,sizeof(candidate)); secret_clear(pairing_account,8); pairing_flow=false; pairing_wait_pin=false;
                if(ok) {
                    live_active=false;
                    set_phase(UI_READY);
                    snprintf(status,sizeof(status),"Console paired and saved. Starting session now.");
                    start_attempt();
                }
                else { remember_failure("Pairing succeeded, but the console could not be saved. Existing pairings are unchanged."); request_stop(false,4); }
            }
            state=rp_state(client);
            /* Pairing can start a fresh session attempt above. Refresh the
             * clock before checking its deadline so the pre-pairing sample
             * cannot underflow against the new attempt_since value. */
            now=(uint32_t)chiaki_time_now_monotonic_ms();
            if(video_pipeline_stats(video).failed) { remember_failure("Video playback failed. Please disconnect and retry."); request_stop(false,8); }
            else if(RP_ATTEMPT_MS && (uint32_t)(now-attempt_since)>=RP_ATTEMPT_MS) {
                remember_failure(phase==UI_DISCOVERY ? "Discovery timed out." : "Session attempt timed out.");
                request_stop(false,3);
            }
            else if(state==RP_FAILED || state==RP_STOPPED || state==RP_STANDBY || state==RP_WRONG_CONSOLE) {

                remember_client_failure(state);
                if(pairing_flow) { secret_clear(pairing_pin,sizeof(pairing_pin)); secret_clear(pairing_account,8); pairing_flow=false; pairing_wait_pin=false; }
                request_stop(false,4);
            } else if(phase==UI_DISCOVERY && (state==RP_CONNECTING || state==RP_CONNECTED)) {
                set_phase(UI_CONNECTING);
                snprintf(status,sizeof(status),"Opening session.\nSaved pairing is unchanged.");
            } else if(phase==UI_DISCOVERY && state==RP_WAKING) {
                snprintf(status,sizeof(status),"Waking console; waiting for it to be ready.\nThis can take up to 60 seconds.\nB cancels; START exits.");
            }
            else if(state==RP_LOGIN_PIN) {
                remember_failure("Console requested a login passcode.");
                snprintf(status,sizeof(status),"Console requires a login passcode.\nDisconnecting without entering it.");
                request_stop(false,6);
            } else if(phase==UI_CONNECTING && state==RP_CONNECTED) {
                set_phase(UI_STREAM);
                snprintf(status,sizeof(status),"Session established. Waiting for decoded video.");
                Input3dsSnapshot neutral={0}; submit_controller(&neutral);
            }
        }
        if(phase==UI_STOPPING) {
            now=(uint32_t)chiaki_time_now_monotonic_ms();

            if(!cleanup_started) {
                do { cleanup_started=rp_request_stop(client); } while(0);
            }

            snprintf(status,sizeof(status),"Disconnecting safely: %lus",
                (unsigned long)((now-phase_since)/1000));
            if(rp_cleanup_failed(client)) {
                snprintf(status,sizeof(status),"Cleanup failed; please retry.\nResources retained; Retry is locked.\nExit cannot complete until cleanup succeeds.");
            }

            bool network_done=cleanup_started && rp_poll_stop(client);

            if(network_done) {

                do { live_active=false; } while(0);

                /* A local exit retains the last image until gfxExit. Only a
                 * completed disconnect returning to the menu needs a redraw. */
                if(!exit_requested) {
#ifdef N3DS_GPU_BILINEAR
                    (void)video_pipeline_clear_top(video);
#else
                    {}
                    void *clear_framebuffer=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);

                    do { memset(clear_framebuffer,0,VIDEO_SCREEN_PIXELS*sizeof(uint16_t)); } while(0);
                    top_flush_pending=true;
#endif
                }
                if(rest_mode_queue_failed)
                    snprintf(status,sizeof(status),"Disconnected; Rest Mode request failed.\nConsole may remain awake.\nA retries; START Exit menu.");
                else if(rest_mode_requested)
                    snprintf(status,sizeof(status),"Console Rest Mode request sent.\nThe console may take a moment to sleep.\nA retries; START Exit menu.");
                else if(attempt_failure[0])
                    snprintf(status,sizeof(status),"Stopped safely.\n%s\nA retries; START Exit menu.",attempt_failure);
                else
                    snprintf(status,sizeof(status),"Disconnected safely.\nA retries; START Exit menu.");
                if(exit_requested) { do { set_phase(UI_FLUSHING); } while(0); }
                else { do { set_phase(UI_DONE); } while(0);  }
            }
        }
        if(phase==UI_STREAM) {
            if(video_pipeline_stats(video).presented) usage_stats_frame(&usage,now);
            const char *picture=video_pipeline_stats(video).presented ?
                "Showing video" : "Connected; waiting for picture";
            AudioStats audio=rp_audio_stats(client);
            char audio_line[64];
            if(audio.failed)
                snprintf(audio_line,sizeof(audio_line),"Audio unavailable (%ld).",(long)audio.last_error);
            else if(audio.running && audio.rate)
                snprintf(audio_line,sizeof(audio_line),"Audio %luk Q%lu Drop%lu U%lu",
                    (unsigned long)(audio.rate/1000),(unsigned long)audio.queued,
                    (unsigned long)audio.dropped,(unsigned long)audio.underruns);
            else snprintf(audio_line,sizeof(audio_line),"Audio starting.");
            if(RP_ATTEMPT_MS) {
                uint32_t elapsed=(uint32_t)chiaki_time_now_monotonic_ms()-attempt_since;
                unsigned remaining=elapsed>=RP_ATTEMPT_MS ? 0 : (RP_ATTEMPT_MS-elapsed+999)/1000;
                snprintf(status,sizeof(status),"%s; disconnecting in %us.\n%s",
                    picture,remaining,audio_line);
            } else
                snprintf(status,sizeof(status),"%s.\n%s\nSELECT magnifier; START Session.",
                    picture,audio_line);
        }
        if(phase==UI_FLUSHING) {
            now=(uint32_t)chiaki_time_now_monotonic_ms();
            snprintf(status,sizeof(status),"Network workers stopped.\nSaving settings: %lus",(unsigned long)((now-phase_since)/1000));

            if(!settings_writer_closed && settings_writer_join(settings_writer)) {

                settings_writer_closed=true;
            }
            do { exit_loop=settings_writer_closed; } while(0);

        }
        /* Optional HTTPS uses the separate HTTP service after streaming ends.
         * Polls never wait. Exit follows its existing path, without a stats wait. */
        usage_stats_tick(&usage,!live_active || phase==UI_STOPPING || phase==UI_FLUSHING,now);
        if(exit_loop) break;
        /* Keep the last frame while native workers finish. Poll again next UI tick; do not draw, present, flush, or wait
         * on a graphics event during teardown. A completed remote disconnect
         * reaches UI_DONE above and can then draw the retry screen. */
        if(phase==UI_STOPPING || phase==UI_FLUSHING) {

            svcSleepThread(16666667);

            continue;
        }
        if(phase==UI_CONNECTING || phase==UI_STREAM) {

            bool presented;
            if(phase==UI_STREAM) {
                ZoomPresentation presentation=zoom_controls_presentation(&zoom_controls);
                bool crop_visible=local_menu==MENU_NONE && zoom_controls.mode!=ZOOM_DIRTY &&
                    (zoom_controls_menu(&zoom_controls) || !controller_view);
                presented=video_pipeline_present_zoom(video,
                    (uint16_t *)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL),
                    crop_visible ? (uint16_t *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL) : NULL,&presentation);
                if(presented && crop_visible) {
                    draw_zoom_legend();
                    if(!controller_view && app_settings.touch_grid && !zoom_controls_menu(&zoom_controls))
                        draw_hud_touch_overlay((uint16_t *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL));
                    bottom_flush_pending=true;
                }
                else if(crop_visible && !video_pipeline_stats(video).presented && waiting_revision!=presentation.revision) {
                    /* No source exists yet: redraw a blank fallback with local
                     * help, rather than leave obsolete connection controls. */
                    memset(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL),0,ZOOM_BOTTOM_PIXELS*sizeof(uint16_t));
                    draw_zoom_legend();
                    if(!controller_view && app_settings.touch_grid && !zoom_controls_menu(&zoom_controls))
                        draw_hud_touch_overlay((uint16_t *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL));
                    bottom_flush_pending=true;
                    waiting_revision=presentation.revision;
                }
            } else presented=video_pipeline_present(video,(uint16_t *)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL));

#ifndef N3DS_GPU_BILINEAR
            if(presented) top_flush_pending=true;
#endif
        }

        bool stream_console=phase==UI_STREAM && (local_menu!=MENU_NONE ||
            zoom_controls.mode==ZOOM_DIRTY || (controller_view && !zoom_controls_menu(&zoom_controls)));
        bool should_draw=(phase!=UI_STREAM || stream_console) && (++redraw>=10 || pressed || zoom_result.mode_changed || bottom_view_changed || (trail_visible && trail_dirty));
        if(should_draw) {
            redraw=0;

            do { draw(); } while(0);

            bottom_flush_pending=true;

        }

        if(top_flush_pending) {

            Result result=GSPGPU_FlushDataCache(gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL),TOP_FRAMEBUFFER_BYTES);

            top_flush_pending=R_FAILED(result);
        }
        if(bottom_flush_pending) {

            Result result=GSPGPU_FlushDataCache(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL),BOTTOM_FRAMEBUFFER_BYTES);

            bottom_flush_pending=R_FAILED(result);
            if(R_SUCCEEDED(result)) {
                /* Queue the completed back buffer for scanout after its
                 * cache flush, then point console output at the new back
                 * buffer before the next iteration draws into it. */
                gfxScreenSwapBuffers(GFX_BOTTOM,false);
                bottom_console->frameBuffer=(u16 *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
            }
        }

        do { gspWaitForVBlank(); } while(0);

    }
    motion_3ds_poll(false);

    do { rp_destroy(client); } while(0);

    do { video_pipeline_destroy(video); } while(0);

    settings_writer_destroy(settings_writer);
    aptUnhook(&usage_apt_hook);
    usage_stats_shutdown(&usage);
    do { socExit(); } while(0);
    do { free(socket_buffer); } while(0);
    do { gfxExit(); } while(0);
    console_store_free(&consoles);

    return 0;
}
