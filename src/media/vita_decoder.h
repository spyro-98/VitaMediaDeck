#ifndef VITAMEDIADECK_MEDIA_VITA_DECODER_H
#define VITAMEDIADECK_MEDIA_VITA_DECODER_H

#include <stddef.h>
#include <stdint.h>

typedef struct VtDecoderStreamHandle {
	void *opaque;
	int (*read)(void *opaque, void *buffer, size_t size);
	int64_t (*seek)(void *opaque, int64_t offset, int whence);
	void (*close)(void *opaque);
	int64_t size;
} VtDecoderStreamHandle;

typedef struct VtDecoderStreamFactory {
	void *opaque;
	int (*open)(void *opaque, VtDecoderStreamHandle *out);
} VtDecoderStreamFactory;

typedef enum VtDecoderBackend {
	VT_DECODER_BACKEND_NONE = 0,
	VT_DECODER_BACKEND_HARDWARE,
	VT_DECODER_BACKEND_SOFTWARE
} VtDecoderBackend;

#define VT_DECODER_MAX_AUDIO_TRACKS 16
#define VT_DECODER_MAX_SUBTITLE_TRACKS 16

typedef struct VtDecoderTrackInfo {
	int stream_index;
	int is_default;
	int channels;
	char language[16];
	char title[64];
	char codec[16];
} VtDecoderTrackInfo;

typedef struct VtDecoderPlayer VtDecoderPlayer;

typedef struct VtDecoderPlayerConfig {
	VtDecoderStreamFactory stream;
	/* NONE means automatic hardware-first selection with software fallback. */
	VtDecoderBackend preferred_backend;
	/* Audio uses a zero-based playable AAC ordinal. Subtitle zero means off;
	 * positive values select a one-based playable text-subtitle ordinal. */
	int audio_track;
	int subtitle_track;
	uint32_t expected_width;
	uint32_t expected_height;
	int expected_fps;
	uint64_t start_position_ms;
	int volume_percent;
	volatile int *cancel_flag;
} VtDecoderPlayerConfig;

typedef struct VtDecoderPlayerStatus {
	int opened;
	int paused;
	int eof;
	int error;
	int hardware_accelerated;
	int direct_rendering;
	int ready_frames;
	int frame_capacity;
	int fps;
	uint32_t width;
	uint32_t height;
	uint64_t video_bitrate_bps;
	uint64_t position_ms;
	uint64_t duration_ms;
	unsigned int frames_decoded;
	unsigned int frames_shown;
	unsigned int frames_dropped;
} VtDecoderPlayerStatus;

void vt_decoder_file_stream_factory(const char *path,
	                                VtDecoderStreamFactory *factory);
int vt_decoder_prepare_hardware_runtime(void);
VtDecoderPlayer *vt_decoder_create(void);
int vt_decoder_open(VtDecoderPlayer *player, const VtDecoderPlayerConfig *config);
void vt_decoder_destroy(VtDecoderPlayer *player);
void vt_decoder_set_paused(VtDecoderPlayer *player, int paused);
void vt_decoder_set_volume(VtDecoderPlayer *player, int percent);
int vt_decoder_seek(VtDecoderPlayer *player, uint64_t position_ms);
int vt_decoder_select_audio_track(VtDecoderPlayer *player, int audio_track,
	                              uint64_t position_ms);
int vt_decoder_select_subtitle_track(VtDecoderPlayer *player,
	                                 int subtitle_track,
	                                 uint64_t position_ms);
int vt_decoder_audio_track_count(const VtDecoderPlayer *player);
int vt_decoder_subtitle_track_count(const VtDecoderPlayer *player);
int vt_decoder_active_audio_track(const VtDecoderPlayer *player);
int vt_decoder_active_subtitle_track(const VtDecoderPlayer *player);
int vt_decoder_audio_track_info(const VtDecoderPlayer *player, int index,
	                            VtDecoderTrackInfo *info);
int vt_decoder_subtitle_track_info(const VtDecoderPlayer *player, int index,
	                               VtDecoderTrackInfo *info);
int vt_decoder_subtitle_text(VtDecoderPlayer *player, uint64_t position_ms,
	                         char *text, size_t text_size);
int vt_decoder_fallback_to_software(VtDecoderPlayer *player,
	                                uint64_t position_ms);
int vt_decoder_present(VtDecoderPlayer *player, int fill_screen);
void vt_decoder_render_complete(VtDecoderPlayer *player);
void vt_decoder_get_status(VtDecoderPlayer *player, VtDecoderPlayerStatus *status);
VtDecoderBackend vt_decoder_backend(const VtDecoderPlayer *player);
const char *vt_decoder_backend_name(const VtDecoderPlayer *player);

#endif
