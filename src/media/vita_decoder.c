#include "media/vita_decoder.h"

#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>

#include <vita_hw_decoder.h>
#include <vita_sw_decoder.h>

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
	return calloc(1, sizeof(VtDecoderPlayer));
}

int vt_decoder_open(VtDecoderPlayer *player, const VtDecoderPlayerConfig *config) {
	if (!player || !config || !config->stream.open) return -1;
	player->source = config->stream;
	player->config = *config;
	VitaHwDecoderPlayerConfig hardware = {
		.stream = { &player->source, hw_stream_open },
		.expected_width = config->expected_width,
		.expected_height = config->expected_height,
		.expected_fps = config->expected_fps,
		.start_position_ms = config->start_position_ms,
		.volume_percent = config->volume_percent,
		.cancel_flag = config->cancel_flag
	};
	player->hardware = vita_hw_decoder_create();
	int result = player->hardware
	           ? vita_hw_decoder_open(player->hardware, &hardware) : -1;
	if (result >= 0) {
		player->backend = VT_DECODER_BACKEND_HARDWARE;
		return 0;
	}
	if (player->hardware) vita_hw_decoder_destroy(player->hardware);
	player->hardware = NULL;
	/* The source contract creates fresh independent cursors, so fallback can
	 * safely reopen after a failed hardware session. */
	VitaSwDecoderPlayerConfig software = {
		.stream = { &player->source, sw_stream_open },
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
	if (result >= 0) player->backend = VT_DECODER_BACKEND_SOFTWARE;
	return result;
}

int vt_decoder_fallback_to_software(VtDecoderPlayer *player,
	                                uint64_t position_ms) {
	if (!player) return -1;
	if (player->software && player->backend == VT_DECODER_BACKEND_SOFTWARE)
		return 0;
	if (player->hardware) {
		vita_hw_decoder_destroy(player->hardware);
		player->hardware = NULL;
	}
	VitaSwDecoderPlayerConfig software = {
		.stream = { &player->source, sw_stream_open },
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
	return player->backend == VT_DECODER_BACKEND_HARDWARE
	     ? vita_hw_decoder_seek(player->hardware, position_ms)
	     : player->backend == VT_DECODER_BACKEND_SOFTWARE
	     ? vita_sw_decoder_seek(player->software, position_ms) : -1;
}

int vt_decoder_present(VtDecoderPlayer *player, int fill_screen) {
	if (!player) return -1;
	return player->backend == VT_DECODER_BACKEND_HARDWARE
	     ? vita_hw_decoder_present(player->hardware, fill_screen)
	     : player->backend == VT_DECODER_BACKEND_SOFTWARE
	     ? vita_sw_decoder_present(player->software, fill_screen) : -1;
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
