#ifndef VITAWAVE_MEDIA_VITA_DECODER_H
#define VITAWAVE_MEDIA_VITA_DECODER_H

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

typedef struct VtDecoderPlayer VtDecoderPlayer;

typedef struct VtDecoderPlayerConfig {
	VtDecoderStreamFactory stream;
	/* NONE means automatic hardware-first selection with software fallback. */
	VtDecoderBackend preferred_backend;
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
int vt_decoder_fallback_to_software(VtDecoderPlayer *player,
	                                uint64_t position_ms);
int vt_decoder_present(VtDecoderPlayer *player, int fill_screen);
void vt_decoder_render_complete(VtDecoderPlayer *player);
void vt_decoder_get_status(VtDecoderPlayer *player, VtDecoderPlayerStatus *status);
VtDecoderBackend vt_decoder_backend(const VtDecoderPlayer *player);
const char *vt_decoder_backend_name(const VtDecoderPlayer *player);

#endif
