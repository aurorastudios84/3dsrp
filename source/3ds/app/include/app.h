/* Shared app-level declarations: paths, config, progress markers. */
#ifndef N3DSRP_APP_H
#define N3DSRP_APP_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Same directory the gate-1 probe used, so ps5.txt / upload.txt / samples
 * already on the card keep working. */
#ifndef RP_ROOT
#define RP_ROOT    "sdmc:/3ds/n3ds-remoteplay/"
#endif
#define RP_CONSOLE_IP "192.168.0.21"
#define RP_CONSOLE_ID "020000000001"
#define RP_CONSOLE_NAME "PS5-Example"
#ifndef RP_ATTEMPT_MS
#define RP_ATTEMPT_MS 0u /* No app-imposed live-session deadline by default. */
#endif
#define RP_CONFIG  RP_ROOT "config.txt"
#define RP_PIN     RP_ROOT "pin.txt"
#define RP_PAIR_FILE RP_ROOT "pairing.bin"
#define RP_CONSOLES_FILE RP_ROOT "consoles.bin"

/* Preserve the proven 360p/recorded480p envelope. The only larger shape
 * admitted is the exact hardware-accepted 960x540 / coded960x544 probe format.
 * 720p remains refused before any MVD call. */
#define RP_ENV_MAX_MBS_DEFAULT   1700u
#define RP_ENV_MAX_DPB_DEFAULT   (6u * 1024u * 1024u)
#define RP_ENV_MAX_LEVEL_DEFAULT 31u
#define RP_ENV_540_MAX_MBS       2040u
#define RP_ENV_540_MAX_DPB       7050240u
#define RP_ENV_540_MAX_LEVEL     32u

typedef struct {
	char host[64];              /* PS5 LAN address                          */
	uint8_t account[8];         /* PSN account id, decoded                  */
	bool have_account;
	char pin[9];                /* temporary/legacy-loaded code; never persisted */
	bool have_pin;
	int width, height, fps;     /* stream profile we ask the console for    */
	bool frame_mode;            /* true: whole AU per call, false: one NAL  */
	uint32_t max_mbs, max_dpb, max_level;
} RpConfig;

/* config.txt: "key = value", '#' comments, unknown keys ignored. Missing file
 * or unreadable values fall back to defaults (360p30 on 192.168.0.21). */
bool cfg_load(RpConfig *cfg);
bool cfg_defaults(RpConfig *cfg);

/* Reads an 8-digit Link Device code from the local setup config. */
bool cfg_read_pin(char pin[9]);

/* Rewrites app-owned settings in RP_CONFIG while preserving unknown lines and
 * comments. A legacy PIN may be loaded into memory but cfg_save always removes
 * it from the file and never persists it. */
bool cfg_save(const RpConfig *cfg);

/* Decodes a PSN account id as typed on the 3DS or as written in chiaki's own
 * config: 12-character Base64 or 16 hex characters. */
bool cfg_decode_account(const char *text, uint8_t out[8]);

/* Legacy helper for reading the address in pairing.bin. The multi-console app
 * uses consoles.bin and no longer selects its active host through config.txt. */
bool pair_host_read(char out[64]);


#endif
