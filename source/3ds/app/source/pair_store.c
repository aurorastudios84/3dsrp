#include "pair_store.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void secret_clear(void *data, size_t size) {
    volatile unsigned char *p = data;
    while(size--) *p++ = 0;
}
static uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t c = ~0u;
    while(n--) { c ^= *p++; for(int j=0;j<8;j++) c = (c >> 1) ^ (0xedb88320u & (0u-(c&1))); }
    return ~c;
}
static void put32(uint8_t *p, uint32_t n) { for(int i=0;i<4;i++) p[i] = n >> (8*i); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static bool suffix(char out[512], const char *path, const char *ext) {
    return snprintf(out,512,"%s%s",path,ext)<512;
}
bool store_recover(const char *path) {
    struct stat st;
    if(stat(path,&st)==0) return S_ISREG(st.st_mode);
    if(errno!=ENOENT) return false;
    char bak[512]; if(!suffix(bak,path,".bak")) return false;
    if(stat(bak,&st)) return errno==ENOENT;
    if(!S_ISREG(st.st_mode)) return false;
    return rename(bak,path)==0;
}
bool store_remove(const char *path) {
    char aux[512];
    /* Remove recovery copies first, so a forgotten pairing cannot reappear. */
    const char *exts[]={".tmp",".bak",""};
    for(size_t i=0;i<3;i++) {
        if(!suffix(aux,path,exts[i])) return false;
        if(remove(aux) && errno!=ENOENT) return false;
    }
    return true;
}
bool store_atomic_write(const char *path, const void *data, size_t size) {
    char tmp[512],bak[512];
    if(!suffix(tmp,path,".tmp") || !suffix(bak,path,".bak") || !store_recover(path)) return false;
    FILE *f = fopen(tmp, "wb");
    if(!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    if(fflush(f)) ok = false;
    if(ok && fsync(fileno(f))) ok = false;
    if(fclose(f)) ok = false;
    if(!ok) { remove(tmp); return false; }
    /* libctru rename-over-existing deletes the destination before moving.
     * Move the old file aside first; never rely on POSIX replacement semantics.
     * A launch interrupted between renames recovers the old .bak file. */
    struct stat st; bool existed=stat(path,&st)==0;
    if(!existed && errno!=ENOENT) { remove(tmp); return false; }
    if(existed) {
        if((remove(bak) && errno!=ENOENT) || rename(path,bak)) { remove(tmp); return false; }
    }
    if(rename(tmp,path)) {
        if(existed) rename(bak,path); /* failed rollback leaves .bak for recovery */
        remove(tmp); return false;
    }
    if(existed) remove(bak);
    return true;
}
static bool valid(const PairRecord *r) {
    struct in_addr addr;
    if(!memchr(r->address, 0, sizeof(r->address)) || inet_pton(AF_INET,r->address,&addr)!=1) return false;
    if(r->legacy) return true;
    if(!pair_key_supported(r->target,r->key_type) || !memchr(r->nickname,0,32) || !r->nickname[0]) return false;
    if(!memchr(r->identity,0,17) || strlen(r->identity)!=12) return false;
    for(int i=0;i<12;i++) if(!((r->identity[i]>='0' && r->identity[i]<='9') || (r->identity[i]>='A' && r->identity[i]<='F'))) return false;
    return true;
}
StoreResult pair_store_load(const char *path, PairRecord *record) {
    if(!store_recover(path)) return STORE_IO;
    FILE *f = fopen(path,"rb");
    if(!f) return errno == ENOENT ? STORE_MISSING : STORE_IO;
    uint8_t b[192] = {0};
    size_t n = fread(b,1,sizeof(b),f);
    bool error = ferror(f); int extra = fgetc(f); fclose(f);
    PairRecord r = {0};
    StoreResult result = STORE_INVALID;
    if(error) { result=STORE_IO; goto done; }
    if(extra != EOF) goto done;
    if(n == 112 && !memcmp(b,"N3DSKEY1",8)) {
        r.legacy=true; r.target=1000100; r.key_type=2;
    } else if(n == 192 && !memcmp(b,"N3DSKEY2",8) && get32(b+188)==crc(b,188)) {
        memcpy(r.identity,b+112,17); memcpy(r.nickname,b+129,32);
        memcpy(r.mac,b+161,6); r.target=get32(b+168); r.key_type=get32(b+172); r.generation=get32(b+176);
    } else goto done;
    memcpy(r.address,b+8,64); memcpy(r.account,b+72,8);
    memcpy(r.regist_key,b+80,16); memcpy(r.key,b+96,16);
    if(!valid(&r)) goto done;
    *record=r; result=STORE_OK;
done:
    secret_clear(b,sizeof(b)); secret_clear(&r,sizeof(r));
    return result;
}
bool pair_store_save(const char *path, const PairRecord *r) {
    if(r->legacy || !valid(r)) return false;
    uint8_t b[192]={0}; memcpy(b,"N3DSKEY2",8);
    memcpy(b+8,r->address,64); memcpy(b+72,r->account,8);
    memcpy(b+80,r->regist_key,16); memcpy(b+96,r->key,16);
    memcpy(b+112,r->identity,17); memcpy(b+129,r->nickname,32);
    memcpy(b+161,r->mac,6); put32(b+168,r->target); put32(b+172,r->key_type); put32(b+176,r->generation);
    put32(b+188,crc(b,188));
    bool ok=store_atomic_write(path,b,sizeof(b)); secret_clear(b,sizeof(b)); return ok;
}
