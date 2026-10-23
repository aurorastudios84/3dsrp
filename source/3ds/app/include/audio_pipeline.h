#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H
#include <chiaki/log.h>
#include <chiaki/audioreceiver.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct AudioPipeline AudioPipeline;
typedef struct {
    uint32_t received, decoded, queued, dropped, underruns, errors;
    uint32_t rate, channels, queue_depth;
    int32_t last_error;
    bool running, failed;
} AudioStats;
AudioPipeline *audio_pipeline_create(ChiakiLog *log);
bool audio_pipeline_start(AudioPipeline *p);
void audio_pipeline_get_sink(AudioPipeline *p, ChiakiAudioSink *sink);
/* Caller must first join the session producer. */
void audio_pipeline_stop(AudioPipeline *p);
bool audio_pipeline_join(AudioPipeline *p);
AudioStats audio_pipeline_stats(AudioPipeline *p);
/* Refuses destruction until the output worker has been joined. */
void audio_pipeline_destroy(AudioPipeline *p);
#endif
