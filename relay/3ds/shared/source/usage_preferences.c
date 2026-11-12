#include "usage_stats.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
static uint32_t crc32(const uint8_t *p,size_t n)
{
    uint32_t crc=~0u;
    for(size_t i=0;i<n;i++) { crc^=p[i];for(unsigned b=0;b<8;b++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u))); }
    return ~crc;
}
bool usage_uuid_valid(const char *id)
{
    if(!id || id[36]) return false;
    for(unsigned i=0;i<36;i++) {
        if(i==8 || i==13 || i==18 || i==23) { if(id[i]!='-') return false; }
        else if(!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f'))) return false;
    }
    return id[14]=='4' && (id[19]=='8' || id[19]=='9' || id[19]=='a' || id[19]=='b');
}
void usage_uuid_from_random(char out[37],const uint8_t random[16])
{
    uint8_t bytes[16];memcpy(bytes,random,16);bytes[6]=(bytes[6]&15u)|0x40u;bytes[8]=(bytes[8]&63u)|0x80u;
    static const char hex[]="0123456789abcdef";unsigned n=0;
    for(unsigned i=0;i<16;i++) {
        if(i==4 || i==6 || i==8 || i==10) out[n++]='-';
        out[n++]=hex[bytes[i]>>4];out[n++]=hex[bytes[i]&15];
    }
    out[n]=0;
}
StoreResult usage_preferences_load(const char *path,UsagePreferences *prefs)
{
    memset(prefs,0,sizeof(*prefs));
    if(!store_recover(path)) return STORE_IO;
    FILE *f=fopen(path,"rb");if(!f) return errno==ENOENT ? STORE_MISSING : STORE_IO;
    uint8_t raw[52];size_t n=fread(raw,1,sizeof(raw),f);int extra=fgetc(f);bool bad=ferror(f);if(fclose(f)) bad=true;
    if(bad) return STORE_IO;
    if(n!=52 || extra!=EOF || (memcmp(raw,"3DSRPUS1",8) && memcmp(raw,"3DSRPUS2",8)) || raw[8]>USAGE_ALLOWED || (!memcmp(raw,"3DSRPUS1",8) && (raw[9] || raw[10] || raw[11]))) return STORE_INVALID;
    uint32_t crc=crc32(raw,48);for(unsigned i=0;i<4;i++) if(raw[48+i]!=(uint8_t)(crc>>(8*i))) return STORE_INVALID;
    UsagePreferences candidate={.choice=(UsageChoice)raw[8],.generation=(uint32_t)raw[9]|(uint32_t)raw[10]<<8|(uint32_t)raw[11]<<16};memcpy(candidate.uuid,raw+12,36);
    if((candidate.uuid[0] && !usage_uuid_valid(candidate.uuid)) || (candidate.choice==USAGE_ALLOWED && !candidate.uuid[0])) return STORE_INVALID;
    *prefs=candidate;return STORE_OK;
}
bool usage_preferences_save(const char *path,const UsagePreferences *prefs)
{
    if(prefs->generation>0xffffffu || (unsigned)prefs->choice>USAGE_ALLOWED || (prefs->uuid[0] && !usage_uuid_valid(prefs->uuid)) ||
       (prefs->choice==USAGE_ALLOWED && !prefs->uuid[0])) return false;
    uint8_t raw[52]={0};memcpy(raw,"3DSRPUS2",8);raw[8]=(uint8_t)prefs->choice;for(unsigned i=0;i<3;i++)raw[9+i]=(uint8_t)(prefs->generation>>(8*i));memcpy(raw+12,prefs->uuid,36);
    uint32_t crc=crc32(raw,48);for(unsigned i=0;i<4;i++) raw[48+i]=(uint8_t)(crc>>(8*i));
    return store_atomic_write(path,raw,sizeof(raw));
}
