#ifndef N3DS_SETTINGS_WRITER_H
#define N3DS_SETTINGS_WRITER_H
#include "app_settings.h"

typedef struct SettingsWriter SettingsWriter;
typedef struct {
    uint32_t requested, completed, persisted;
} SettingsWriteStatus;

/* One owner for settings.bin and its recovery files. Requests copy a complete
 * snapshot; the mutex is never held across filesystem operations. */
SettingsWriter *settings_writer_create(const char *path);
uint32_t settings_writer_submit(SettingsWriter *writer,const AppSettings *settings);
SettingsWriteStatus settings_writer_status(SettingsWriter *writer);
/* Drain the latest request. Poll join; never release resources before the
 * worker's actual kernel join. Failed writes are reported, not retried forever. */
void settings_writer_stop(SettingsWriter *writer);
bool settings_writer_join(SettingsWriter *writer);
void settings_writer_destroy(SettingsWriter *writer);
#endif
