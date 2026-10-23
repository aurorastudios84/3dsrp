#include "console_store.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <strings.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <limits.h>

/* Versioned SD database. pairing.bin is deliberately never migrated or removed. */
#define DB_HEADER 24u
#define DB_RECORD 192u
static uint32_t crc32(const uint8_t *p,size_t n) { uint32_t c=~0u; while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));} return ~c; }
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void pack(uint8_t *b,const PairRecord *r){memset(b,0,DB_RECORD);memcpy(b,"N3DSKEY2",8);memcpy(b+8,r->address,64);memcpy(b+72,r->account,8);memcpy(b+80,r->regist_key,16);memcpy(b+96,r->key,16);memcpy(b+112,r->identity,17);memcpy(b+129,r->nickname,32);memcpy(b+161,r->mac,6);b[167]=r->direct?1:0;put32(b+168,r->target);put32(b+172,r->key_type);put32(b+176,r->generation);put32(b+188,crc32(b,188));}
/* Reuse the canonical record validator/decoder through a private temporary file is
 * undesirable on SD; validate fields here before publishing the loaded record. */
static bool unpack(const uint8_t *b,PairRecord *r){struct in_addr ip;if(memcmp(b,"N3DSKEY2",8)||get32(b+188)!=crc32(b,188))return false;memset(r,0,sizeof(*r));memcpy(r->address,b+8,64);memcpy(r->account,b+72,8);memcpy(r->regist_key,b+80,16);memcpy(r->key,b+96,16);memcpy(r->identity,b+112,17);memcpy(r->nickname,b+129,32);memcpy(r->mac,b+161,6);if(b[167]>1)return false;r->direct=b[167]!=0;r->target=get32(b+168);r->key_type=get32(b+172);r->generation=get32(b+176);if(!memchr(r->address,0,64)||inet_pton(AF_INET,r->address,&ip)!=1||!memchr(r->identity,0,17)||strlen(r->identity)!=12||!memchr(r->nickname,0,32)||!r->nickname[0]||!pair_key_supported(r->target,r->key_type))return false;for(unsigned i=0;i<12;i++)if(!((r->identity[i]>='0'&&r->identity[i]<='9')||(r->identity[i]>='A'&&r->identity[i]<='F')))return false;return true;}
void console_store_free(ConsoleStore *s) {
 if(!s)return;
 if(s->records){secret_clear(s->records,(size_t)s->capacity*sizeof(*s->records));free(s->records);}
 memset(s,0,sizeof(*s));
}
static bool reserve(ConsoleStore *s,uint32_t count) {
 if(count<=s->capacity)return true;
 if(count>UINT32_MAX-3u || (uint64_t)count*sizeof(PairRecord)>SIZE_MAX)return false;
 uint32_t capacity=count;
 if(s->capacity && s->capacity<=(UINT32_MAX-3u)/2u && (uint64_t)s->capacity*2u*sizeof(PairRecord)<=SIZE_MAX)
  if(capacity<s->capacity*2u)capacity=s->capacity*2u;
 PairRecord *records=calloc(capacity,sizeof(*records));
 if(!records)return false;
 if(s->count)memcpy(records,s->records,(size_t)s->count*sizeof(*records));
 if(s->records){secret_clear(s->records,(size_t)s->capacity*sizeof(*records));free(s->records);}
 s->records=records;s->capacity=capacity;return true;
}
bool console_store_clone(ConsoleStore *destination,const ConsoleStore *source) {
 if(!destination||!source||destination==source)return false;
 ConsoleStore copy={0};
 if(!reserve(&copy,source->count))return false;
 copy.count=source->count;copy.selected=source->selected;
 if(copy.count)memcpy(copy.records,source->records,(size_t)copy.count*sizeof(*copy.records));
 console_store_free(destination);*destination=copy;return true;
}
void console_store_rollback(ConsoleStore *s,ConsoleStore *backup) {
 console_store_free(s);*s=*backup;memset(backup,0,sizeof(*backup));
}
StoreResult console_store_load(const char *path,ConsoleStore *s){
 if(!s||!store_recover(path))return STORE_IO;
 FILE *f=fopen(path,"rb");
 if(!f)return errno==ENOENT?STORE_MISSING:STORE_IO;
 uint8_t header[DB_HEADER];
 if(fread(header,1,sizeof(header),f)!=sizeof(header)){bool io=ferror(f);fclose(f);return io?STORE_IO:STORE_INVALID;}
 uint32_t count=get32(header+12),selected=get32(header+16);
 uint64_t length=DB_HEADER+(uint64_t)count*DB_RECORD;
 if(memcmp(header,"N3DSDB01",8)||get32(header+8)!=1||count>UINT32_MAX-3u||
    selected>=(count?count:1)||length>SIZE_MAX||length>LONG_MAX){fclose(f);return STORE_INVALID;}
 if(fseek(f,0,SEEK_END)){fclose(f);return STORE_IO;}
 long actual=ftell(f);
 if(actual<0){fclose(f);return STORE_IO;}
 if((uint64_t)actual!=length){fclose(f);return STORE_INVALID;}
 if(fseek(f,DB_HEADER,SEEK_SET)){fclose(f);return STORE_IO;}
 size_t size=(size_t)count*DB_RECORD;
 uint8_t *bytes=malloc(size?size:1);
 if(!bytes){fclose(f);return STORE_IO;}
 StoreResult result=STORE_INVALID;ConsoleStore out={0};
 if(fread(bytes,1,size,f)!=size || ferror(f))result=STORE_IO;
 else if(crc32(bytes,size)==get32(header+20)) {
  if(!reserve(&out,count))result=STORE_IO;
  else {
   out.count=count;out.selected=selected;result=STORE_OK;
   for(uint32_t i=0;i<count;i++)if(!unpack(bytes+(size_t)i*DB_RECORD,&out.records[i])){result=STORE_INVALID;break;}
  }
 }
 fclose(f);secret_clear(bytes,size);free(bytes);
 if(result==STORE_OK){console_store_free(s);*s=out;}else console_store_free(&out);
 return result;
}
bool console_store_save(const char *path,const ConsoleStore *s){
 if(!s||(s->count&&(!s->records||s->selected>=s->count))||s->count>s->capacity)return false;
 uint64_t length=DB_HEADER+(uint64_t)s->count*DB_RECORD;
 if(length>SIZE_MAX)return false;
 uint8_t *b=calloc(1,(size_t)length);if(!b)return false;
 memcpy(b,"N3DSDB01",8);put32(b+8,1);put32(b+12,s->count);put32(b+16,s->count?s->selected:0);
 for(uint32_t i=0;i<s->count;i++)pack(b+DB_HEADER+(size_t)i*DB_RECORD,&s->records[i]);
 put32(b+20,crc32(b+DB_HEADER,(size_t)s->count*DB_RECORD));
 bool ok=store_atomic_write(path,b,(size_t)length);secret_clear(b,(size_t)length);free(b);return ok;
}
bool console_store_add(ConsoleStore *s,const PairRecord *r){
 if(!s||!r||!pair_key_supported(r->target,r->key_type)||s->count>=UINT32_MAX-3u)return false;
 for(uint32_t i=0;i<s->count;i++) {
  const PairRecord *old=&s->records[i];
  bool same_id=!strcasecmp(old->identity,r->identity),same_mac=!memcmp(old->mac,r->mac,6);
  /* A conflicting identity is not an alias. Never overwrite existing keys. */
  if(same_id!=same_mac || (same_id && old->target!=r->target))return false;
  if(same_id && !memcmp(old->account,r->account,8) && !strcmp(old->address,r->address))return false;
 }
 PairRecord copy=*r;
 if(!reserve(s,s->count+1)){secret_clear(&copy,sizeof(copy));return false;}
 s->records[s->count]=copy;s->selected=s->count++;secret_clear(&copy,sizeof(copy));return true;
}
bool console_address_is_public(const char *address){
 struct in_addr ip;
 if(!address||inet_pton(AF_INET,address,&ip)!=1)return false;
 uint32_t host=ntohl(ip.s_addr);
 return !((host>>24)==10 || (host>>24)==127 || (host>>20)==0xac1 || (host>>16)==0xc0a8 || (host>>16)==0xa9fe);
}
bool console_store_add_address(ConsoleStore *s,unsigned index,const char *address){
 struct in_addr ip;
 if(!s||index>=s->count||!address||inet_pton(AF_INET,address,&ip)!=1)return false;
 PairRecord alias=s->records[index];
 snprintf(alias.address,sizeof(alias.address),"%s",address);
 alias.direct=console_address_is_public(address);
 bool ok=console_store_add(s,&alias);secret_clear(&alias,sizeof(alias));return ok;
}
bool console_store_remove(ConsoleStore *s,unsigned i){if(!s||i>=s->count)return false;secret_clear(&s->records[i],sizeof(s->records[i]));for(unsigned j=i+1;j<s->count;j++)s->records[j-1]=s->records[j];s->count--;secret_clear(&s->records[s->count],sizeof(s->records[s->count]));if(!s->count)s->selected=0;else if(s->selected==i)s->selected=i?s->selected-1:0;else if(s->selected>i)s->selected--;return true;}

bool console_store_edit_address(ConsoleStore *s,unsigned index,const char *address) {
 struct in_addr ip;
 if(!s||index>=s->count||!address||inet_pton(AF_INET,address,&ip)!=1)return false;
 const PairRecord *record=&s->records[index];
 for(uint32_t i=0;i<s->count;i++)if(i!=index &&
    !strcasecmp(record->identity,s->records[i].identity) &&
    !memcmp(record->account,s->records[i].account,8) && !strcmp(address,s->records[i].address))return false;
 snprintf(s->records[index].address,sizeof(s->records[index].address),"%s",address);
 return true;
}
