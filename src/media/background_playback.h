#ifndef VITAMEDIADECK_MEDIA_BACKGROUND_PLAYBACK_H
#define VITAMEDIADECK_MEDIA_BACKGROUND_PLAYBACK_H

#include <stdint.h>

#define VT_BACKGROUND_TITLE_MAX 160
#define VT_BACKGROUND_CHANNEL_MAX 112
#define VT_BACKGROUND_MEDIA_ID_MAX 64
#define VT_BACKGROUND_ARTWORK_MAX 512

typedef enum {
	VT_BACKGROUND_IDLE = 0,
	VT_BACKGROUND_PREPARING,
	VT_BACKGROUND_READY,
	VT_BACKGROUND_BUFFERING,
	VT_BACKGROUND_PLAYING,
	VT_BACKGROUND_PAUSED,
	VT_BACKGROUND_ERROR
} VtBackgroundPlaybackState;

typedef struct {
	VtBackgroundPlaybackState state;
	int visible;
	int error;
	uint32_t activation_serial;
	uint64_t position_ms;
	uint64_t duration_ms;
	uint32_t video_width;
	uint32_t video_height;
	char video_id[VT_BACKGROUND_MEDIA_ID_MAX];
	char title[VT_BACKGROUND_TITLE_MAX];
	char channel[VT_BACKGROUND_CHANNEL_MAX];
	char thumbnail_url[VT_BACKGROUND_ARTWORK_MAX];
} VtBackgroundPlaybackSnapshot;

typedef int (*VtBackgroundFullscreenResume)(uint64_t position_ms, void *ctx);

void vt_background_playback_init(void);

/* Opens a file already stored on the console. The service never performs
 * network I/O and never takes ownership of the file. */
int vt_background_playback_prepare_local(const char *media_path,
	                                     const char *media_id,
	                                     const char *title,
	                                     const char *artist,
	                                     const char *artwork_path,
	                                     uint64_t duration_ms);
int vt_background_playback_prepare_local_video(const char *media_path,
	                                           const char *media_id,
	                                           const char *title,
	                                           const char *artist,
	                                           const char *artwork_path,
	                                           uint64_t duration_ms,
	                                           int audio_track);

int vt_background_playback_prepared(void);
int vt_background_playback_activate(uint64_t start_position_ms);
void vt_background_playback_toggle_pause(void);
void vt_background_playback_seek_to(uint64_t position_ms);
void vt_background_playback_seek_relative(int64_t delta_ms);

/* Optional hand-off hook used to restore a minimized local video fullscreen at
 * the position currently owned by the compact background player. */
void vt_background_playback_set_fullscreen_resume(
	VtBackgroundFullscreenResume resume, void *ctx);
int vt_background_playback_resume_fullscreen(void);

void vt_background_playback_request_stop(void);
void vt_background_playback_stop(void);
void vt_background_playback_shutdown(void);
int vt_background_playback_snapshot(VtBackgroundPlaybackSnapshot *out);

/* Local video sources expose a decoded surface to the compact mini player.
 * Audio-only sources return zero and the UI draws artwork. */
int vt_background_playback_draw_video(float x, float y,
	                                  float width, float height);
void vt_background_playback_video_render_complete(void);

#endif /* VITAMEDIADECK_MEDIA_BACKGROUND_PLAYBACK_H */
