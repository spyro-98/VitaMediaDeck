#ifndef VITAMEDIADECK_MEDIA_VIDEO_THUMBNAIL_H
#define VITAMEDIADECK_MEDIA_VIDEO_THUMBNAIL_H

#include <stdint.h>

#include <vita2d.h>

#include "network/network_source.h"

/* Starts one low-priority software-only H.264 worker.  Decoding and cache I/O
 * always happen on that worker; the UI thread only uploads a completed RGB565
 * buffer from pump(). */
int vt_video_thumbnail_init(void);
void vt_video_thumbnail_resume(void);
void vt_video_thumbnail_suspend(void);
/* Stops thumbnail work, fences and releases cached GPU covers before the video
 * decoder claims CDRAM, and briefly waits for the CPU worker to acknowledge
 * cancellation. Safe to call repeatedly. */
void vt_video_thumbnail_prepare_playback(void);
void vt_video_thumbnail_pump(void);

/* Non-blocking. Returns a cached GPU texture or queues the path and returns
 * NULL. Sidecar artwork remains the caller's first choice. */
vita2d_texture *vt_video_thumbnail_get(const char *path, uint64_t source_size);
vita2d_texture *vt_video_thumbnail_get_priority(
	const char *path, uint64_t source_size, int priority);

/* Local still images and sidecar artwork share the same asynchronous worker,
 * fixed RGB565 output, disk cache, prioritization, and GPU texture budget.
 * The full image decoder is never called from a grid render loop. */
vita2d_texture *vt_image_thumbnail_get(
	const char *path, uint64_t source_size);
vita2d_texture *vt_image_thumbnail_get_priority(
	const char *path, uint64_t source_size, int priority);

/* Remote previews use the same bounded worker and disposable RGB565 cache as
 * local files. Credentials are copied only into transient in-memory requests,
 * never serialized, and are cleared after the request is consumed. */
vita2d_texture *vt_video_thumbnail_get_remote(
	const VtNetworkSource *source,
	const VtNetworkCredential *credential,
	const char *path, uint64_t source_size);
vita2d_texture *vt_video_thumbnail_get_remote_priority(
	const VtNetworkSource *source,
	const VtNetworkCredential *credential,
	const char *path, uint64_t source_size, int priority);

/* Must run before vita2d_fini(), so cached textures can be released safely. */
void vt_video_thumbnail_shutdown(void);

#endif
