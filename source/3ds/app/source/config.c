/* App configuration on the SD card.
 *
 * config.txt is "key = value" with '#' comments; every value is optional, so a
 * card that only has the PS5 address still runs. pin.txt is the Link Device
 * code, dropped in over FTP (or typed on screen) and consumed once.
 */
#include "app.h"
#include "pair_store.h"
#include <errno.h>
#include <strings.h>

#include <chiaki/base64.h>

#include <ctype.h>
#include <stdlib.h>

static char *skip_ws(char *s) {
	while (*s == ' ' || *s == '\t') s++;
	return s;
}

static void rstrip(char *s) {
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
		s[--n] = 0;
}

static bool hex_decode(const char *s, uint8_t *out, size_t n) {
	for (size_t i = 0; i < n; i++) {
		int hi, lo;
		char a = s[i * 2], b = s[i * 2 + 1];
		hi = (a >= '0' && a <= '9') ? a - '0' : (a >= 'a' && a <= 'f') ? a - 'a' + 10 :
		     (a >= 'A' && a <= 'F') ? a - 'A' + 10 : -1;
		lo = (b >= '0' && b <= '9') ? b - '0' : (b >= 'a' && b <= 'f') ? b - 'a' + 10 :
		     (b >= 'A' && b <= 'F') ? b - 'A' + 10 : -1;
		if (hi < 0 || lo < 0) return false;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

/* Accepts the 12-character Base64 form chiaki writes in its own config, or 16
 * hex characters, so either can be pasted in from the host. */
bool cfg_decode_account(const char *s, uint8_t out[8]) {
    if(!s) return false;
    size_t n = strlen(s);
    uint8_t tmp[8] = {0};
    bool ok = false;
    if(n == 16) ok = hex_decode(s, tmp, 8);
    else if(n == 12 && s[11] == '=') {
        size_t size = sizeof(tmp);
        char canonical[13];
        ok = chiaki_base64_decode(s,n,tmp,&size) == CHIAKI_ERR_SUCCESS && size==8 &&
             chiaki_base64_encode(tmp,8,canonical,sizeof(canonical)) == CHIAKI_ERR_SUCCESS &&
             strcmp(s,canonical)==0;
    }
    if(ok) memcpy(out,tmp,8);
    secret_clear(tmp,sizeof(tmp));
    return ok;
}

bool cfg_defaults(RpConfig *cfg) {
	memset(cfg, 0, sizeof(*cfg));
	snprintf(cfg->host, sizeof(cfg->host), "%s", "192.168.0.21");
	cfg->width = 640;
	cfg->height = 360;
	cfg->fps = 30;
	cfg->frame_mode = false;
	cfg->max_mbs = RP_ENV_MAX_MBS_DEFAULT;
	cfg->max_dpb = RP_ENV_MAX_DPB_DEFAULT;
	cfg->max_level = RP_ENV_MAX_LEVEL_DEFAULT;
	return true;
}

static void set_profile(RpConfig *cfg, const char *v) {
	if (!strncmp(v, "360", 3)) { cfg->width = 640; cfg->height = 360; }
	else if (!strncmp(v, "540", 3)) { cfg->width = 960; cfg->height = 540; }
	else if (!strncmp(v, "720", 3)) { cfg->width = 1280; cfg->height = 720; }
	if (strstr(v, "p60")) cfg->fps = 60; else cfg->fps = 30;
}

bool cfg_load(RpConfig *cfg) {
	cfg_defaults(cfg);

	if(!store_recover(RP_CONFIG)) return false;
	FILE *f = fopen(RP_CONFIG, "r");
	if (!f) return false;

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char *p = skip_ws(line);
		if (*p == '#' || *p == '\r' || *p == '\n' || !*p) continue;
		char *eq = strchr(p, '=');
		if (!eq) continue;
		*eq = 0;
		char *key = p, *val = skip_ws(eq + 1);
		rstrip(key);
		rstrip(val);

		if (!strcasecmp(key, "host")) snprintf(cfg->host, sizeof(cfg->host), "%s", val);
		else if (!strcasecmp(key, "account")) cfg->have_account = cfg_decode_account(val, cfg->account);
		else if (!strcasecmp(key, "pin")) {
            size_t n=strlen(val); bool digits=n==8;
            for(size_t i=0;i<n && digits;i++) if(val[i]<'0' || val[i]>'9') digits=false;
            if(digits) { memcpy(cfg->pin,val,9); cfg->have_pin=true; }
        }
		else if (!strcasecmp(key, "profile")) set_profile(cfg, val);
		else if (!strcasecmp(key, "mode")) cfg->frame_mode = !strcmp(val, "frame");
		else if (!strcasecmp(key, "maxmbs")) cfg->max_mbs = (uint32_t)strtoul(val, NULL, 0);
		else if (!strcasecmp(key, "maxdpb")) cfg->max_dpb = (uint32_t)strtoul(val, NULL, 0);
		else if (!strcasecmp(key, "maxlevel")) cfg->max_level = (uint32_t)strtoul(val, NULL, 0);

	}
	fclose(f);
	return true;
}

/* Legacy PIN files are never consumed by the guided setup. */
bool cfg_read_pin(char pin[9]) {
    RpConfig cfg;
    cfg_load(&cfg);
    if(!cfg.have_pin) { pin[0]=0; return false; }
    memcpy(pin,cfg.pin,9); secret_clear(&cfg,sizeof(cfg)); return true;
}

/* Every key this app owns, compared case-insensitively: config.txt is edited by
 * hand as often as by the setup screen, and "HOST" is not a reason to ignore a
 * line. */
static bool key_is(const char *key, const char *want) {
	while (*key && *want) {
		char a = *key++;
		if (a >= 'A' && a <= 'Z') a += 32;
		if (a != *want++) return false;
	}
	return !*key && !*want;
}

/* Rewrites config.txt from the live config. The old file is read into memory
 * first, so lines this app does not own -- and comments -- survive a save; keys
 * that are missing are added. The Link Device code is single use: it is never
 * written, so nothing can try to spend it twice. */
bool cfg_save(const RpConfig *cfg) {
    char old[8192] = {0}, output[9216] = {0}, acct[24] = {0};
    if(cfg->have_account && chiaki_base64_encode(cfg->account,8,acct,sizeof(acct)) != CHIAKI_ERR_SUCCESS) return false;
    if(!store_recover(RP_CONFIG)) return false;
    FILE *f=fopen(RP_CONFIG,"r");
    if(f) {
        size_t n=fread(old,1,sizeof(old)-1,f);
        bool bad=ferror(f) || fgetc(f)!=EOF || memchr(old,0,n)!=NULL;
        fclose(f);
        if(bad) { secret_clear(old,sizeof(old)); return false; }
    } else if(errno!=ENOENT) return false;
    size_t used=0;
    for(char *at=old; *at;) {
        char *nl=strchr(at,'\n');
        size_t n=nl ? (size_t)(nl-at)+1 : strlen(at);
        char key[64]={0}; size_t k=0;
        char *q=at; while(*q==' ' || *q=='\t') q++;
        while(k<sizeof(key)-1 && *q && *q!='=' && !isspace((unsigned char)*q)) key[k++]=*q++;
        bool owned=key_is(key,"host") || key_is(key,"account") || key_is(key,"pin") ||
                   key_is(key,"profile") || key_is(key,"mode") || key_is(key,"upload");
        if(!owned) { memcpy(output+used,at,n); used+=n; if(!nl) output[used++]='\n'; }
        at+=n;
    }
    int n=snprintf(output+used,sizeof(output)-used,
        "host = %s\nprofile = %dp%d\nmode = %s\n",cfg->host,cfg->height,cfg->fps,cfg->frame_mode ? "frame":"nal");
    if(n<0 || (size_t)n>=sizeof(output)-used) goto fail;
    used+=(size_t)n;
    if(acct[0]) {
        n=snprintf(output+used,sizeof(output)-used,"account = %s\n",acct);
        if(n<0 || (size_t)n>=sizeof(output)-used) goto fail;
        used+=(size_t)n;
    }

    {
        bool ok=store_atomic_write(RP_CONFIG,output,used);
        secret_clear(old,sizeof(old)); secret_clear(output,sizeof(output)); secret_clear(acct,sizeof(acct));
        return ok;
    }
fail:
    secret_clear(old,sizeof(old)); secret_clear(output,sizeof(output)); secret_clear(acct,sizeof(acct));
    return false;
}

bool pair_host_read(char out[64]) {
    PairRecord r={0};
    bool ok=pair_store_load(RP_PAIR_FILE,&r)==STORE_OK;
    if(ok) memcpy(out,r.address,64);
    secret_clear(&r,sizeof(r)); return ok;
}

