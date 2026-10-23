#include "settings_writer.h"
#include <chiaki/thread.h>
#include <chiaki/time.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct SettingsWriter {
    ChiakiThread thread;
    ChiakiMutex mutex;
    ChiakiCond changed;
    char path[256];
    AppSettings latest;
    SettingsWriteStatus status;
    bool stopping, joined;
    atomic_bool done;
};

static void *write_settings(void *arg)
{
    SettingsWriter *writer=arg;

    for(;;) {
        chiaki_mutex_lock(&writer->mutex);
        while(!writer->stopping && writer->status.requested==writer->status.completed)
            chiaki_cond_wait(&writer->changed,&writer->mutex);
        if(writer->status.requested==writer->status.completed) {
            chiaki_mutex_unlock(&writer->mutex);
            break;
        }
        AppSettings snapshot=writer->latest;
        uint32_t revision=writer->status.requested;
        chiaki_mutex_unlock(&writer->mutex);

        bool ok=app_settings_save(writer->path,&snapshot);

        chiaki_mutex_lock(&writer->mutex);
        writer->status.completed=revision;
        if(ok) writer->status.persisted=revision;
        chiaki_mutex_unlock(&writer->mutex);
    }
    atomic_store(&writer->done,true);
    return NULL;
}

SettingsWriter *settings_writer_create(const char *path)
{
    if(!path || strlen(path)>=sizeof(((SettingsWriter *)0)->path)) return NULL;
    SettingsWriter *writer=calloc(1,sizeof(*writer));
    if(!writer) return NULL;
    strcpy(writer->path,path);
    atomic_init(&writer->done,false);
    if(chiaki_mutex_init(&writer->mutex,false)!=CHIAKI_ERR_SUCCESS) { free(writer); return NULL; }
    if(chiaki_cond_init(&writer->changed)!=CHIAKI_ERR_SUCCESS) {
        chiaki_mutex_fini(&writer->mutex); free(writer); return NULL;
    }
    if(chiaki_thread_create(&writer->thread,write_settings,writer)!=CHIAKI_ERR_SUCCESS) {
        chiaki_cond_fini(&writer->changed); chiaki_mutex_fini(&writer->mutex);
        free(writer); return NULL;
    }
    return writer;
}

uint32_t settings_writer_submit(SettingsWriter *writer,const AppSettings *settings)
{
    if(!writer || !app_settings_valid(settings)) return 0;
    chiaki_mutex_lock(&writer->mutex);
    uint32_t revision=0;
    if(!writer->stopping && writer->status.requested!=UINT32_MAX) {
        writer->latest=*settings;
        revision=++writer->status.requested;
        chiaki_cond_signal(&writer->changed);
    }
    chiaki_mutex_unlock(&writer->mutex);

    return revision;
}

SettingsWriteStatus settings_writer_status(SettingsWriter *writer)
{
    if(!writer) return (SettingsWriteStatus){0};
    chiaki_mutex_lock(&writer->mutex);
    SettingsWriteStatus status=writer->status;
    chiaki_mutex_unlock(&writer->mutex);
    return status;
}

void settings_writer_stop(SettingsWriter *writer)
{
    if(!writer) return;
    chiaki_mutex_lock(&writer->mutex);
    writer->stopping=true;
    chiaki_cond_signal(&writer->changed);
    chiaki_mutex_unlock(&writer->mutex);
}

bool settings_writer_join(SettingsWriter *writer)
{
    if(!writer || writer->joined) return true;
    if(!atomic_load(&writer->done) ||
       chiaki_thread_timedjoin(&writer->thread,NULL,0)!=CHIAKI_ERR_SUCCESS) return false;
    writer->joined=true;
    return true;
}

void settings_writer_destroy(SettingsWriter *writer)
{
    if(!writer || !writer->joined) return;
    chiaki_cond_fini(&writer->changed);
    chiaki_mutex_fini(&writer->mutex);
    free(writer);
}
