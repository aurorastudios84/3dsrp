#ifndef THREEDSRP_USAGE_STATS_H
#define THREEDSRP_USAGE_STATS_H
#include <stdbool.h>
#include <stdint.h>
#include "pair_store.h"
#define USAGE_TIMEOUT_MS 5000u
#define USAGE_MAX_SECONDS 604800u
#define USAGE_QUEUE_CAPACITY 128u
#define USAGE_DAY_MS 86400000u
#define USAGE_FILE RP_ROOT "usage.bin"
#define USAGE_PENDING_FILE RP_ROOT "usage-pending.bin"
typedef enum { USAGE_UNASKED, USAGE_DECLINED, USAGE_ALLOWED } UsageChoice;
typedef struct { UsageChoice choice; char uuid[37]; uint32_t generation; } UsagePreferences;
typedef struct { char session_id[37],day[11]; uint32_t milliseconds; } UsageReport;
typedef struct {
    bool (*init)(void); bool (*open)(void); bool (*trust)(void); bool (*headers)(void);
    bool (*body)(const char *uuid,const UsageReport *report); bool (*begin_async)(void);
    int (*poll)(void); /* 0 pending, 1 accepted, 2 permanent rejection, -1 retry on next launch. */
    void (*cancel)(void); void (*close)(void); void (*exit)(void);
} UsageHttpOps;
typedef struct {
    const UsageHttpOps *http;
    bool enabled,streaming,busy,service_open,context_open,loaded,identity_failed;
    uint32_t stream_started,request_started,generation;
    uint64_t calendar_ms;
    char uuid[37],session_id[37];
    const char *journal_path;
    UsageReport reports[USAGE_QUEUE_CAPACITY];
    bool attempted[USAGE_QUEUE_CAPACITY];
    unsigned count,in_flight;
} UsageStats;
bool usage_uuid_valid(const char *uuid);
void usage_uuid_from_random(char uuid[37],const uint8_t random[16]);
StoreResult usage_preferences_load(const char *path,UsagePreferences *prefs);
bool usage_preferences_save(const char *path,const UsagePreferences *prefs);
bool usage_random_uuid(char uuid[37]);
bool usage_day_from_epoch(int64_t seconds,char day[11]);
bool usage_day_valid(const char *day);
bool usage_journal_load(const char *path,const char *uuid,uint32_t generation,UsageReport *reports,unsigned *count);
bool usage_journal_save(const char *path,const char *uuid,uint32_t generation,const UsageReport *reports,unsigned count);
void usage_stats_init(UsageStats *stats,const UsageHttpOps *http);
void usage_stats_set_journal(UsageStats *stats,const char *path);
void usage_stats_enable(UsageStats *stats,const UsagePreferences *prefs);
void usage_stats_hold(UsageStats *stats,uint32_t now);
void usage_stats_begin_session(UsageStats *stats);
void usage_stats_frame(UsageStats *stats,uint32_t now);
void usage_stats_end_session(UsageStats *stats,uint32_t now);
void usage_stats_suspend(UsageStats *stats,uint32_t now);
void usage_stats_tick(UsageStats *stats,bool network_allowed,uint32_t now);
void usage_stats_shutdown(UsageStats *stats);
extern const UsageHttpOps usage_http_3ds;
#endif
