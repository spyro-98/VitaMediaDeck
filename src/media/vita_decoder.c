#include "media/vita_decoder.h"

#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>

#include <vita_hw_decoder.h>
#include <vita_sw_decoder.h>

#include "media/media_tracks.h"

_Static_assert(sizeof(VtDecoderPlayerStatus) == sizeof(VitaHwDecoderPlayerStatus),
	           "hardware status ABI changed");
_Static_assert(sizeof(VtDecoderPlayerStatus) == sizeof(VitaSwDecoderPlayerStatus),
	           "software status ABI changed");

typedef struct LocalFileCursor {
	SceUID file;
} LocalFileCursor;

struct VtDecoderPlayer {
	VtDecoderBackend backend;
	VtDecoderStreamFactory source;
	VtDecoderPlayerConfig config;
	VitaHwDecoderPlayer *hardware;
	VitaSwDecoderPlayer *software;
	VtDecoderTrackInfo audio_tracks[VT_DECODER_MAX_AUDIO_TRACKS];
	VtDecoderTrackInfo subtitle_tracks[VT_DECODER_MAX_SUBTITLE_TRACKS];
	int audio_track_count;
	int subtitle_track_count;
	int active_audio_track;
	int active_subtitle_track;
	VtSubtitleReader *subtitles;
};

static int local_read(void *opaque, void *buffer, size_t size) {
	LocalFileCursor *cursor = opaque;
	return cursor ? sceIoRead(cursor->file, buffer, size) : -1;
}

static int64_t local_seek(void *opaque, int64_t offset, int origin) {
	LocalFileCursor *cursor = opaque;
	return cursor ? sceIoLseek(cursor->file, offset, origin) : -1;
}

static void local_close(void *opaque) {
	LocalFileCursor *cursor = opaque;
	if (!cursor) return;
	if (cursor->file >= 0) sceIoClose(cursor->file);
	free(cursor);
}

static int local_open(void *opaque, VtDecoderStreamHandle *out) {
	const char *path = opaque;
	if (!path || !out) return -1;
	LocalFileCursor *cursor = calloc(1, sizeof(*cursor));
	if (!cursor) return -1;
	cursor->file = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (cursor->file < 0) {
		free(cursor);
		return -1;
	}
	int64_t size = sceIoLseek(cursor->file, 0, SCE_SEEK_END);
	if (size < 0 || sceIoLseek(cursor->file, 0, SCE_SEEK_SET) < 0) {
		local_close(cursor);
		return -1;
	}
	*out = (VtDecoderStreamHandle){ cursor, local_read, local_seek,
	                                local_close, size };
	return 0;
}

void vt_decoder_file_stream_factory(const char *path,
	                                VtDecoderStreamFactory *factory) {
	if (!factory) return;
	factory->opaque = (void *)path;
	factory->open = local_open;
}

static int hw_stream_open(void *opaque, VitaHwDecoderStreamHandle *out) {
	VtDecoderStreamFactory *factory = opaque;
	VtDecoderStreamHandle cursor;
	int result = factory && factory->open ? factory->open(factory->opaque, &cursor) : -1;
	if (result < 0) return result;
	out->opaque = cursor.opaque;
	out->read = cursor.read;
	out->seek = cursor.seek;
	out->close = cursor.close;
	out->size = cursor.size;
	return 0;
}

static int sw_stream_open(void *opaque, VitaSwDecoderStreamHandle *out) {
	VtDecoderStreamFactory *factory = opaque;
	VtDecoderStreamHandle cursor;
	int result = factory && factory->open ? factory->open(factory->opaque, &cursor) : -1;
	if (result < 0) return result;
	out->opaque = cursor.opaque;
	out->read = cursor.read;
	out->seek = cursor.seek;
	out->close = cursor.close;
	out->size = cursor.size;
	return 0;
}

int vt_decoder_prepare_hardware_runtime(void) {
	return vita_hw_decoder_prepare_runtime();
}

VtDecoderPlayer *vt_decoder_create(void) {
	VtDecoderPlayer *player = calloc(1, sizeof(VtDecoderPlayer));
	if (!player) return NULL;
	player->subtitles = vt_subtitle_reader_create();
	if (!player->subtitles) {
		free(player);
		return NULL;
	}
	return player;
}

static void open_selected_subtitles(VtDecoderPlayer *player,
	                                uint64_t position_ms) {
	if (!player || !player->subtitles) return;
	if (player->active_subtitle_track <= 0 ||
	    player->active_subtitle_track > player->subtitle_track_count) {
		vt_subtitle_reader_close(player->subtitles);
		player->active_subtitle_track = 0;
		return;
	}
	int index = player->active_subtitle_track - 1;
	if (vt_subtitle_reader_open(player->subtitles, &player->source,
	                            player->subtitle_tracks[index].stream_index,
	                            position_ms,
	                            player->config.cancel_flag) < 0)
		player->active_subtitle_track = 0;
}

int vt_decoder_open(VtDecoderPlayer *player, const VtDecoderPlayerConfig *config) {
	if (!player || !config || !config->stream.open) return -1;
	player->source = config->stream;
	player->config = *config;
	player->audio_track_count = 0;
	player->subtitle_track_count = 0;
	int probe_result = vt_media_tracks_probe(
	    &player->source, player->audio_tracks, &player->audio_track_count,
	    player->subtitle_tracks, &player->subtitle_track_count,
	    config->cancel_flag);
	/* Track discovery uses the same factory and demux configuration as
	 * playback. Do not silently downgrade to an unselectable first track if
	 * the discovery cursor fails, especially for a transient remote source. */
	if (probe_result < 0) return probe_result;
	player->active_audio_track = config->audio_track;
	if (player->active_audio_track < 0 ||
	    player->active_audio_track >= player->audio_track_count)
		player->active_audio_track = 0;
	player->active_subtitle_track = config->subtitle_track;
	if (player->active_subtitle_track < 0 ||
	    player->active_subtitle_track > player->subtitle_track_count)
		player->active_subtitle_track = 0;
	player->config.audio_track = player->active_audio_track;
	player->config.subtitle_track = player->active_subtitle_track;
	VtDecoderBackend preference = config->preferred_backend;
	if (preference != VT_DECODER_BACKEND_HARDWARE &&
	    preference != VT_DECODER_BACKEND_SOFTWARE)
		preference = VT_DECODER_BACKEND_NONE;
	int result = -1;
	if (preference != VT_DECODER_BACKEND_SOFTWARE) {
		VitaHwDecoderPlayerConfig hardware = {
			.stream = { &player->source, hw_stream_open },
			.audio_track = player->active_audio_track,
			.expected_width = config->expected_width,
			.expected_height = config->expected_height,
			.expected_fps = config->expected_fps,
			.start_position_ms = config->start_position_ms,
			.volume_percent = config->volume_percent,
			.cancel_flag = config->cancel_flag
		};
		player->hardware = vita_hw_decoder_create();
		result = player->hardware
		       ? vita_hw_decoder_open(player->hardware, &hardware) : -1;
		if (result >= 0) {
			player->backend = VT_DECODER_BACKEND_HARDWARE;
			open_selected_subtitles(player, config->start_position_ms);
			return 0;
		}
		if (player->hardware) vita_hw_decoder_destroy(player->hardware);
		player->hardware = NULL;
		if (preference == VT_DECODER_BACKEND_HARDWARE) return result;
	}
	/* The source contract creates fresh independent cursors, so fallback can
	 * safely reopen after a failed hardware session. A software preference
	 * arrives here directly without initializing the hardware backend. */
	VitaSwDecoderPlayerConfig software = {
		.stream = { &player->source, sw_stream_open },
		.audio_track = player->active_audio_track,
		.expected_width = config->expected_width,
		.expected_height = config->expected_height,
		.expected_fps = config->expected_fps,
		.start_position_ms = config->start_position_ms,
		.volume_percent = config->volume_percent,
		.cancel_flag = config->cancel_flag
	};
	player->software = vita_sw_decoder_create();
	result = player->software
	       ? vita_sw_decoder_open(player->software, &software) : -1;
	if (result >= 0) {
		player->backend = VT_DECODER_BACKEND_SOFTWARE;
		open_selected_subtitles(player, config->start_position_ms);
	}
	return result;
}

int vt_decoder_fallback_to_software(VtDecoderPlayer *player,
	                                uint64_t position_ms) {
	if (!player) return -1;
	if (player->config.preferred_backend == VT_DECODER_BACKEND_HARDWARE)
		return -1;
	if (player->software && player->backend == VT_DECODER_BACKEND_SOFTWARE)
		return 0;
	if (player->hardware) {
		vita_hw_decoder_destroy(player->hardware);
		player->hardware = NULL;
	}
	VitaSwDecoderPlayerConfig software = {
		.stream = { &player->source, sw_stream_open },
		.audio_track = player->active_audio_track,
		.expected_width = player->config.expected_width,
		.expected_height = player->config.expected_height,
		.expected_fps = player->config.expected_fps,
		.start_position_ms = position_ms,
		.volume_percent = player->config.volume_percent,
		.cancel_flag = player->config.cancel_flag
	};
	player->software = vita_sw_decoder_create();
	int result = player->software
	           ? vita_sw_decoder_open(player->software, &software) : -1;
	player->backend = result >= 0
	                ? VT_DECODER_BACKEND_SOFTWARE : VT_DECODER_BACKEND_NONE;
	return result;
}

void vt_decoder_destroy(VtDecoderPlayer *player) {
	if (!player) return;
	vt_subtitle_reader_destroy(player->subtitles);
	if (player->hardware) vita_hw_decoder_destroy(player->hardware);
	if (player->software) vita_sw_decoder_destroy(player->software);
	free(player);
}

void vt_decoder_set_paused(VtDecoderPlayer *player, int paused) {
	if (!player) return;
	if (player->backend == VT_DECODER_BACKEND_HARDWARE)
		vita_hw_decoder_set_paused(player->hardware, paused);
	else if (player->backend == VT_DECODER_BACKEND_SOFTWARE)
		vita_sw_decoder_set_paused(player->software, paused);
}

void vt_decoder_set_volume(VtDecoderPlayer *player, int percent) {
	if (!player) return;
	player->config.volume_percent = percent;
	if (player->backend == VT_DECODER_BACKEND_HARDWARE)
		vita_hw_decoder_set_volume(player->hardware, percent);
	else if (player->backend == VT_DECODER_BACKEND_SOFTWARE)
		vita_sw_decoder_set_volume(player->software, percent);
}

int vt_decoder_seek(VtDecoderPlayer *player, uint64_t position_ms) {
	if (!player) return -1;
	int result = player->backend == VT_DECODER_BACKEND_HARDWARE
	     ? vita_hw_decoder_seek(player->hardware, position_ms)
	     : player->backend == VT_DECODER_BACKEND_SOFTWARE
	     ? vita_sw_decoder_seek(player->software, position_ms) : -1;
	if (result >= 0) open_selected_subtitles(player, position_ms);
	return result;
}

int vt_decoder_select_audio_track(VtDecoderPlayer *player, int audio_track,
	                              uint64_t position_ms) {
	if (!player || audio_track < 0 || audio_track >= player->audio_track_count)
		return -1;
	if (audio_track == player->active_audio_track) return 0;
	int result = player->backend == VT_DECODER_BACKEND_HARDWARE
	           ? vita_hw_decoder_select_audio_track(player->hardware,
	                                                audio_track, position_ms)
	           : player->backend == VT_DECODER_BACKEND_SOFTWARE
	           ? vita_sw_decoder_select_audio_track(player->software,
	                                                audio_track, position_ms)
	           : -1;
	if (result >= 0) {
		player->active_audio_track = audio_track;
		player->config.audio_track = audio_track;
	}
	return result;
}

int vt_decoder_select_subtitle_track(VtDecoderPlayer *player,
	                                 int subtitle_track,
	                                 uint64_t position_ms) {
	if (!player || subtitle_track < 0 ||
	    subtitle_track > player->subtitle_track_count) return -1;
	if (subtitle_track == player->active_subtitle_track) return 0;
	if (subtitle_track == 0) {
		vt_subtitle_reader_disable(player->subtitles);
		player->active_subtitle_track = 0;
		player->config.subtitle_track = 0;
		return 0;
	}
	int index = subtitle_track - 1;
	int result = vt_subtitle_reader_open(
	    player->subtitles, &player->source,
	    player->subtitle_tracks[index].stream_index, position_ms,
	    player->config.cancel_flag);
	if (result >= 0) {
		player->active_subtitle_track = subtitle_track;
		player->config.subtitle_track = subtitle_track;
	} else {
		player->active_subtitle_track = 0;
		player->config.subtitle_track = 0;
	}
	return result;
}

int vt_decoder_audio_track_count(const VtDecoderPlayer *player) {
	return player ? player->audio_track_count : 0;
}

int vt_decoder_subtitle_track_count(const VtDecoderPlayer *player) {
	return player ? player->subtitle_track_count : 0;
}

int vt_decoder_active_audio_track(const VtDecoderPlayer *player) {
	return player ? player->active_audio_track : 0;
}

int vt_decoder_active_subtitle_track(const VtDecoderPlayer *player) {
	return player ? player->active_subtitle_track : 0;
}

int vt_decoder_audio_track_info(const VtDecoderPlayer *player, int index,
	                            VtDecoderTrackInfo *info) {
	if (!player || !info || index < 0 || index >= player->audio_track_count)
		return -1;
	*info = player->audio_tracks[index];
	return 0;
}

int vt_decoder_subtitle_track_info(const VtDecoderPlayer *player, int index,
	                               VtDecoderTrackInfo *info) {
	if (!player || !info || index < 0 || index >= player->subtitle_track_count)
		return -1;
	*info = player->subtitle_tracks[index];
	return 0;
}

int vt_decoder_subtitle_text(VtDecoderPlayer *player, uint64_t position_ms,
	                         char *text, size_t text_size) {
	if (!player) return -1;
	int result = vt_subtitle_reader_text(player->subtitles, position_ms,
	                                    text, text_size);
	if (result < 0) {
		vt_subtitle_reader_disable(player->subtitles);
		player->active_subtitle_track = 0;
		player->config.subtitle_track = 0;
	}
	return result;
}

int vt_decoder_present(VtDecoderPlayer *player, int fill_screen) {
	if (!player) return -1;
	return player->backend == VT_DECODER_BACKEND_HARDWARE
	     ? vita_hw_decoder_present(player->hardware, fill_screen)
	     : player->backend == VT_DECODER_BACKEND_SOFTWARE
	     ? vita_sw_decoder_present(player->software, fill_screen) : -1;
}

int vt_decoder_present_rect(VtDecoderPlayer *player, float x, float y,
	                        float width, float height, int fill_rect) {
	if (!player) return -1;
	return player->backend == VT_DECODER_BACKEND_HARDWARE
	     ? vita_hw_decoder_present_rect(player->hardware, x, y, width, height,
	                                    fill_rect)
	     : player->backend == VT_DECODER_BACKEND_SOFTWARE
	     ? vita_sw_decoder_present_rect(player->software, x, y, width, height,
	                                    fill_rect) : -1;
}

void vt_decoder_render_complete(VtDecoderPlayer *player) {
	if (!player) return;
	if (player->backend == VT_DECODER_BACKEND_HARDWARE)
		vita_hw_decoder_render_complete(player->hardware);
	else if (player->backend == VT_DECODER_BACKEND_SOFTWARE)
		vita_sw_decoder_render_complete(player->software);
}

void vt_decoder_get_status(VtDecoderPlayer *player, VtDecoderPlayerStatus *status) {
	if (!status) return;
	memset(status, 0, sizeof(*status));
	if (!player) return;
	if (player->backend == VT_DECODER_BACKEND_HARDWARE) {
		VitaHwDecoderPlayerStatus source;
		vita_hw_decoder_get_status(player->hardware, &source);
		memcpy(status, &source, sizeof(*status));
		status->hardware_accelerated = 1;
	} else if (player->backend == VT_DECODER_BACKEND_SOFTWARE) {
		VitaSwDecoderPlayerStatus source;
		vita_sw_decoder_get_status(player->software, &source);
		memcpy(status, &source, sizeof(*status));
		status->hardware_accelerated = 0;
	}
}

VtDecoderBackend vt_decoder_backend(const VtDecoderPlayer *player) {
	return player ? player->backend : VT_DECODER_BACKEND_NONE;
}

const char *vt_decoder_backend_name(const VtDecoderPlayer *player) {
	return player && player->backend == VT_DECODER_BACKEND_HARDWARE ? "hardware" :
	       player && player->backend == VT_DECODER_BACKEND_SOFTWARE ? "software" :
	       "none";
}
