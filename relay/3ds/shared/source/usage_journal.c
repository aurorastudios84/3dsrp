#include "usage_stats.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#define HEADER_SIZE 52u
#define RECORD_SIZE 52u
#define MAX_SIZE (HEADER_SIZE+RECORD_SIZE*USAGE_QUEUE_CAPACITY+4u)
static uint32_t crc32(const uint8_t *p,size_t n)
{
 uint32_t crc=~0u;for(size_t i=0;i<n;i++){crc^=p[i];for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}return ~crc;
}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
bool usage_day_from_epoch(int64_t epoch,char day[11])
{
 if(epoch<1577836800LL || epoch>4102444799LL)return false;
 /* Civil date from Unix days; independent of time_t width/timezone globals. */
 int64_t z=epoch/86400+719468,era=z/146097;unsigned doe=(unsigned)(z-era*146097);
 unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
 int year=(int)(yoe+era*400);unsigned doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
 unsigned date=doy-(153*mp+2)/5+1,month=mp<10?mp+3:mp-9;year+=month<=2;
 return snprintf(day,11,"%04d-%02u-%02u",year,month,date)==10;
}
bool usage_day_valid(const char *day)
{
 if(!day || day[10] || day[4]!='-' || day[7]!='-')return false;
 unsigned y=0,m=0,d=0;for(unsigned i=0;i<10;i++)if(i!=4&&i!=7){if(day[i]<'0'||day[i]>'9')return false;}
 y=(unsigned)((day[0]-'0')*1000+(day[1]-'0')*100+(day[2]-'0')*10+day[3]-'0');m=(unsigned)((day[5]-'0')*10+day[6]-'0');d=(unsigned)((day[8]-'0')*10+day[9]-'0');
 if(y<2020 || y>2099 || m<1 || m>12)return false;
 static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};unsigned max=days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));return d>=1 && d<=max;
}
static bool valid(const UsageReport *r){return usage_uuid_valid(r->session_id)&&usage_day_valid(r->day)&&r->milliseconds<=USAGE_DAY_MS;}
bool usage_journal_save(const char *path,const char *uuid,uint32_t generation,const UsageReport *reports,unsigned count)
{
 if(!path || !usage_uuid_valid(uuid) || count>USAGE_QUEUE_CAPACITY)return false;
 if(!count)return store_remove(path);
 uint8_t raw[MAX_SIZE]={0};memcpy(raw,"3DSRPJ02",8);memcpy(raw+8,uuid,36);put32(raw+44,generation);put32(raw+48,count);
 for(unsigned i=0;i<count;i++){
  if(!valid(&reports[i]))return false;
  uint8_t *p=raw+HEADER_SIZE+i*RECORD_SIZE;memcpy(p,reports[i].session_id,36);memcpy(p+36,reports[i].day,10);put32(p+48,reports[i].milliseconds);
 }
 size_t size=HEADER_SIZE+count*RECORD_SIZE;put32(raw+size,crc32(raw,size));return store_atomic_write(path,raw,size+4);
}
bool usage_journal_load(const char *path,const char *uuid,uint32_t generation,UsageReport *reports,unsigned *count)
{
 *count=0;if(!path || !store_recover(path))return false;FILE *f=fopen(path,"rb");if(!f)return false;
 uint8_t raw[MAX_SIZE];size_t size=fread(raw,1,sizeof(raw),f);int extra=fgetc(f);bool bad=ferror(f);if(fclose(f))bad=true;
 if(bad || extra!=EOF || size<HEADER_SIZE+4 || memcmp(raw,"3DSRPJ02",8) || memcmp(raw+8,uuid,36))return false;
 if(get32(raw+44)!=generation)return false;
 unsigned n=get32(raw+48);if(n>USAGE_QUEUE_CAPACITY || size!=HEADER_SIZE+n*RECORD_SIZE+4 || get32(raw+size-4)!=crc32(raw,size-4))return false;
 for(unsigned i=0;i<n;i++){
  const uint8_t *p=raw+HEADER_SIZE+i*RECORD_SIZE;UsageReport r={0};memcpy(r.session_id,p,36);memcpy(r.day,p+36,10);r.milliseconds=get32(p+48);
  if(p[46] || p[47] || !valid(&r))return false;
  for(unsigned j=0;j<i;j++)if(!strcmp(reports[j].session_id,r.session_id)&&!strcmp(reports[j].day,r.day))return false;
  reports[i]=r;
 }
 *count=n;return true;
}
