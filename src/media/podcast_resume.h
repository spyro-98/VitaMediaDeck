#ifndef VMD_PODCAST_RESUME_H
#define VMD_PODCAST_RESUME_H
#include <stdint.h>

typedef struct {
    char id[16];
    uint64_t position_ms, duration_ms;
    uint64_t history_order; /* Base generation prevents stale checkpoint replay. */
} VtPodcastResumePoint;
/* Called on the saver thread; return only a decoder-confirmed position. */
typedef int (*VtPodcastResumeSample)(VtPodcastResumePoint *out);
/* Init/recovery runs on the UI thread, after playback_history_init(). */
int vt_podcast_resume_init(VtPodcastResumeSample sample);
/* Memory-only fallback if importing the compact checkpoint could not be saved. */
int vt_podcast_resume_restore(const char *id,uint64_t order,uint64_t duration_ms,uint64_t *position_ms);
void vt_podcast_resume_request(void);
/* Capture final progress without I/O. Retains the last valid sample on errors. */
int vt_podcast_resume_finish(const char *id, VtPodcastResumePoint *out);
void vt_podcast_resume_shutdown(void);
#endif
