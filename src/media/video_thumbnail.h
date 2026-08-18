#ifndef VITATUBE_MEDIA_VIDEO_THUMBNAIL_H
#define VITATUBE_MEDIA_VIDEO_THUMBNAIL_H

#include <stdint.h>

#include <vita2d.h>

/* Starts one low-priority software-only H.264 worker.  Decoding and cache I/O
 * always happen on that worker; the UI thread only uploads a completed RGB565
 * buffer from pump(). */
int vt_video_thumbnail_init(void);
void vt_video_thumbnail_resume(void);
void vt_video_thumbnail_suspend(void);
void vt_video_thumbnail_pump(void);

/* Non-blocking. Returns a cached GPU texture or queues the path and returns
 * NULL. Sidecar artwork remains the caller's first choice. */
vita2d_texture *vt_video_thumbnail_get(const char *path, uint64_t source_size);

/* Must run before vita2d_fini(), so cached textures can be released safely. */
void vt_video_thumbnail_shutdown(void);

#endif
