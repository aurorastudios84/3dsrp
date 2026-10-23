#include "app_settings.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static uint32_t checksum(const uint8_t *bytes,size_t size)
{
    uint32_t crc=~0u;
    for(size_t i=0;i<size;i++) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0]|(uint16_t)p[1]<<8; }
static bool view_valid(const ZoomView *view)
{
    return view && view->zoom_q>=ZOOM_MIN_Q && view->zoom_q<=ZOOM_MAX_Q && !(view->zoom_q&1u);
}
static void put16(uint8_t *p,uint16_t value) { p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8); }
void app_settings_defaults(AppSettings *settings)
{
    if(settings) *settings=(AppSettings){
        .resolution=APP_RESOLUTION_360P,.bitrate_kbps=2000,.fps=30,.onscreen_helpers=false,
        .touch_grid=true,
        .bottom_view=APP_BOTTOM_BLANK,
        .push_options_on_menu=false,
        .hud_hold_buttons=1u<<8, /* Physical L; matches RP_INPUT_L. */
        .reading_view={ZOOM_CENTER,ZOOM_CENTER,ZOOM_DEFAULT_Q},
        .hud_view={ZOOM_CENTER,ZOOM_CENTER,ZOOM_DEFAULT_Q},.migration_pending=false};
}
bool app_settings_valid(const AppSettings *settings)
{
    if(!settings || !view_valid(&settings->reading_view) || !view_valid(&settings->hud_view) ||
       settings->resolution!=APP_RESOLUTION_360P ||
       settings->fps!=30 || settings->bitrate_kbps!=2000 || (settings->hud_hold_buttons & ~0xfffu) ||
       (unsigned)settings->bottom_view>APP_BOTTOM_HOLD_HUD ||
       (settings->bottom_view==APP_BOTTOM_HOLD_HUD && !settings->hud_hold_buttons)) return false;
    return true;
}
bool app_settings_show_hud(const AppSettings *settings,uint32_t held,bool gameplay)
{
    return settings && gameplay && (settings->bottom_view==APP_BOTTOM_HUD ||
        (settings->bottom_view==APP_BOTTOM_HOLD_HUD && (held & settings->hud_hold_buttons)));
}
const char *app_settings_bottom_view_label(const AppSettings *settings)
{
    switch(settings->bottom_view) {
    case APP_BOTTOM_BLANK: return "Blank";
    case APP_BOTTOM_HUD: return "HUD";
    case APP_BOTTOM_HOLD_HUD: return "Hold for HUD";
    default: return "Unknown";
    }
}
const char *app_settings_resolution_label(const AppSettings *settings)
{
    (void)settings;
    return "360p (640x360)";
}
bool app_settings_cycle(AppSettings *settings,unsigned row,int direction)
{
    if(!app_settings_valid(settings) || (direction!=-1 && direction!=1)) return false;
    switch(row) {
    case APP_SETTING_TOUCH_GRID: settings->touch_grid=!settings->touch_grid; break;
    case APP_SETTING_BOTTOM_VIEW:
        settings->bottom_view=(AppBottomView)(((int)settings->bottom_view+direction+3)%3);
        if(settings->bottom_view==APP_BOTTOM_HOLD_HUD && !settings->hud_hold_buttons)
            settings->hud_hold_buttons=1u<<8; /* L when entering from an empty legacy mask. */
        break;
    case APP_SETTING_PUSH_OPTIONS: settings->push_options_on_menu=!settings->push_options_on_menu; break;
    default: return false;
    }
    return true;
}
StoreResult app_settings_load(const char *path,AppSettings *settings)
{
    if(!settings) return STORE_INVALID;
    app_settings_defaults(settings);
    if(!path) return STORE_INVALID;
    if(!store_recover(path)) return STORE_IO;
    FILE *file=fopen(path,"rb");
    if(!file) return errno==ENOENT ? STORE_MISSING : STORE_IO;
    uint8_t bytes[36]={0};
    size_t size=fread(bytes,1,sizeof(bytes),file);
    int extra=fgetc(file);
    bool error=ferror(file)!=0;
    if(fclose(file)) error=true;
    if(error) return STORE_IO;
    if(extra!=EOF) return STORE_INVALID;
    AppSettings candidate;
    app_settings_defaults(&candidate);
    bool migration_pending=false;
    bool bottom_version=size==36 && !memcmp(bytes,"N3DSOP11",8);
    bool hold_version=bottom_version || (size==36 && !memcmp(bytes,"N3DSOP10",8));
    AppBottomView saved_view=APP_BOTTOM_BLANK;
    if(hold_version) {
        if(bytes[8]!=(bottom_version ? 11 : 10) ||
           (bottom_version ? bytes[30]>APP_BOTTOM_HOLD_HUD : bytes[30]!=0) || bytes[31] ||
           get32(bytes+32)!=checksum(bytes,32)) return STORE_INVALID;
        saved_view=(AppBottomView)bytes[30];
        if(bottom_version && ((bytes[17]&1u)!= (saved_view!=APP_BOTTOM_HUD))) return STORE_INVALID;
        candidate.hud_hold_buttons=get16(bytes+28);
        /* The first 28 bytes retain v9 fields; validate through that path. */
        memcpy(bytes,"N3DSOPT9",8); bytes[8]=9;
        uint32_t crc=checksum(bytes,28);
        for(unsigned i=0;i<4;i++) bytes[28+i]=(uint8_t)(crc>>(8*i));
        size=32;
    }
    if(size==16 && !memcmp(bytes,"N3DSOPT1",8)) {
        if(bytes[8]!=1 || bytes[9]>1 || bytes[10] || bytes[11] ||
           get32(bytes+12)!=checksum(bytes,12)) return STORE_INVALID;
        migration_pending=true;
    } else if(size==24 && (!memcmp(bytes,"N3DSOPT2",8) || !memcmp(bytes,"N3DSOPT3",8) ||
                           !memcmp(bytes,"N3DSOPT4",8) || !memcmp(bytes,"N3DSOPT5",8))) {
        bool legacy=!memcmp(bytes,"N3DSOPT2",8);
        bool helpers_version=!memcmp(bytes,"N3DSOPT4",8);
        bool grid_version=!memcmp(bytes,"N3DSOPT5",8);
        if(bytes[8]!=(legacy ? 2 : grid_version ? 5 : helpers_version ? 4 : 3) || bytes[9]>1 ||
           ((helpers_version || grid_version) ? bytes[11]>1 : bytes[11]!=0) ||
           (grid_version ? bytes[16]>1 || (bytes[17] || bytes[18] || bytes[19]) : get32(bytes+16)!=0) ||
           get32(bytes+20)!=checksum(bytes,20)) return STORE_INVALID;
        if(legacy && bytes[10]>1) return STORE_INVALID;
        candidate.resolution=legacy && bytes[10]==1 ? APP_RESOLUTION_540P : (AppResolution)bytes[10];
        candidate.bitrate_kbps=get32(bytes+12);
        if(helpers_version || grid_version) candidate.onscreen_helpers=bytes[11]!=0;
        if(grid_version) candidate.touch_grid=bytes[16]!=0;
        migration_pending=true;
    } else if(size==32 && (!memcmp(bytes,"N3DSOPT6",8) || !memcmp(bytes,"N3DSOPT7",8) ||
                           !memcmp(bytes,"N3DSOPT8",8) || !memcmp(bytes,"N3DSOPT9",8))) {
        bool fps_version=!memcmp(bytes,"N3DSOPT9",8);
        bool view_version=!memcmp(bytes,"N3DSOPT7",8) || !memcmp(bytes,"N3DSOPT8",8) || fps_version;
        bool options_version=!memcmp(bytes,"N3DSOPT8",8) || fps_version;
        if(bytes[8]!=(fps_version ? 9 : options_version ? 8 : view_version ? 7 : 6) || bytes[9]>1 || bytes[11]>1 || bytes[16]>1 ||
           (options_version ? bytes[17]>3 : view_version ? bytes[17]>1 : bytes[17]!=0) ||
           bytes[19]<ZOOM_MIN_Q || bytes[19]>ZOOM_MAX_Q || (bytes[19]&1u) ||
           bytes[18]<ZOOM_MIN_Q || bytes[18]>ZOOM_MAX_Q || (bytes[18]&1u) ||
           get32(bytes+28)!=checksum(bytes,28)) return STORE_INVALID;
        if(fps_version) candidate.fps=bytes[10];
        else candidate.resolution=(AppResolution)bytes[10];
        candidate.bitrate_kbps=get32(bytes+12);
        candidate.onscreen_helpers=bytes[11]!=0;
        candidate.touch_grid=bytes[16]!=0;
        if(view_version) candidate.bottom_view=(bytes[17]&1u) ? APP_BOTTOM_BLANK : APP_BOTTOM_HUD;
        candidate.push_options_on_menu=options_version && (bytes[17]&2u)!=0;
        candidate.reading_view=(ZoomView){get16(bytes+20),get16(bytes+22),bytes[18]};
        candidate.hud_view=(ZoomView){get16(bytes+24),get16(bytes+26),bytes[19]};
        migration_pending=!fps_version;
    } else return STORE_INVALID;
    if(migration_pending) {
        /* Validate old domains before normalization, preserving corruption
         * rejection instead of silently accepting arbitrary resolutions/rates. */
        static const uint32_t legacy_rates[]={500,1000,1500,2000,3000,4000,6000,8000};
        bool old_rate=false;
        for(unsigned i=0;i<sizeof(legacy_rates)/sizeof(legacy_rates[0]);i++)
            if(candidate.bitrate_kbps==legacy_rates[i]) old_rate=true;
        if(!old_rate || candidate.resolution>APP_RESOLUTION_540P) return STORE_INVALID;
        candidate.resolution=APP_RESOLUTION_360P;
        candidate.bitrate_kbps=2000;
        candidate.fps=30;
    } else {
        /* Same v9 layout; former valid FPS/rates migrate without losing prefs. */
        uint32_t rate=candidate.bitrate_kbps;
        if((candidate.fps!=30 && candidate.fps!=60) ||
           (rate!=1500 && rate!=2000 && rate!=3000 && rate!=4000)) return STORE_INVALID;
        migration_pending=!hold_version || candidate.fps!=30 || rate!=2000;
        candidate.fps=30;
        candidate.bitrate_kbps=2000;
    }
    /* Legacy Blank also enabled held-button HUD. Preserve that behavior and
     * the user's mask, while new Blank remains independent of the mask. */
    if(bottom_version) candidate.bottom_view=saved_view;
    else if(candidate.bottom_view==APP_BOTTOM_BLANK && candidate.hud_hold_buttons)
        candidate.bottom_view=APP_BOTTOM_HOLD_HUD;
    migration_pending=migration_pending || !bottom_version;
    /* Older versions permitted Hold with no buttons. That behaved as Blank;
     * keep its behavior without loading an unusable Hold configuration. */
    if(candidate.bottom_view==APP_BOTTOM_HOLD_HUD && !candidate.hud_hold_buttons) {
        candidate.bottom_view=APP_BOTTOM_BLANK;
        migration_pending=true;
    }
    if(!app_settings_valid(&candidate)) return STORE_INVALID;
    candidate.migration_pending=migration_pending;
    *settings=candidate;
    return STORE_OK;
}
bool app_settings_save(const char *path,const AppSettings *settings)
{
    if(!path || !app_settings_valid(settings)) return false;
    uint8_t bytes[36]={0};
    memcpy(bytes,"N3DSOP11",8);
    bytes[8]=11; bytes[9]=0;
    bytes[10]=settings->fps;
    bytes[11]=settings->onscreen_helpers ? 1 : 0;
    bytes[16]=settings->touch_grid ? 1 : 0;
    bytes[17]=(settings->bottom_view!=APP_BOTTOM_HUD ? 1u : 0u) |
        (settings->push_options_on_menu ? 2u : 0u);
    for(unsigned i=0;i<4;i++) bytes[12+i]=(uint8_t)(settings->bitrate_kbps>>(8*i));
    bytes[18]=settings->reading_view.zoom_q; bytes[19]=settings->hud_view.zoom_q;
    put16(bytes+20,settings->reading_view.center_x); put16(bytes+22,settings->reading_view.center_y);
    put16(bytes+24,settings->hud_view.center_x); put16(bytes+26,settings->hud_view.center_y);
    put16(bytes+28,settings->hud_hold_buttons);
    bytes[30]=(uint8_t)settings->bottom_view;
    uint32_t crc=checksum(bytes,32);
    for(unsigned i=0;i<4;i++) bytes[32+i]=(uint8_t)(crc>>(8*i));
    return store_atomic_write(path,bytes,sizeof(bytes));
}
