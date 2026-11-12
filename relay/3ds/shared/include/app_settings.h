#ifndef N3DS_APP_SETTINGS_H
#define N3DS_APP_SETTINGS_H
#include "pair_store.h"
#include "zoom.h"
/* Nonzero values identify legacy records only; all live settings require 360p. */
typedef enum { APP_RESOLUTION_360P=0, APP_RESOLUTION_480P=1,
    APP_RESOLUTION_540P=2 } AppResolution;
typedef enum { APP_BOTTOM_BLANK=0, APP_BOTTOM_HUD=1,
    APP_BOTTOM_HOLD_HUD=2 } AppBottomView;
enum { APP_SETTING_TOUCH_GRID, APP_SETTING_BOTTOM_VIEW,
    APP_SETTING_PUSH_OPTIONS, APP_SETTING_BACK, APP_SETTING_COUNT };
typedef struct {
    AppResolution resolution;
    uint32_t bitrate_kbps;
    uint8_t fps;
    bool onscreen_helpers;
    bool touch_grid;
    AppBottomView bottom_view;
    bool push_options_on_menu;
    /* Physical input bits 0..11: ABXY, D-pad, L/R/ZL/ZR. Hold mode requires one. */
    uint16_t hud_hold_buttons;
    ZoomView reading_view;
    ZoomView hud_view;
    bool migration_pending;
} AppSettings;
void app_settings_defaults(AppSettings *settings);
bool app_settings_valid(const AppSettings *settings);
bool app_settings_show_hud(const AppSettings *settings,uint32_t held,bool gameplay);
/* Cycle editable Settings rows; stream profile remains fixed at 360p/30/2000. */
bool app_settings_cycle(AppSettings *settings,unsigned row,int direction);
const char *app_settings_bottom_view_label(const AppSettings *settings);
const char *app_settings_resolution_label(const AppSettings *settings);
/* Invalid/missing input restores stream defaults 360p/30/2000. */
StoreResult app_settings_load(const char *path,AppSettings *settings);
bool app_settings_save(const char *path,const AppSettings *settings);
#endif
