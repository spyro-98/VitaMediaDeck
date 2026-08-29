#include "media/vita_decoder.h"

#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <libavutil/error.h>
#include <vita_hw_decoder.h>
#include <vita_sw_decoder.h>

#include "common/text_log.h"
#include "media/media_tracks.h"

_Static_assert(sizeof(VtDecoderPlayerStatus) == sizeof(VitaHwDecoderPlayerStatus),
	           "hardware status ABI changed");
_Static_assert(sizeof(VtDecoderPlayerStatus) == sizeof(VitaSwDecoderPlayerStatus),
	           "software status ABI changed");
_Static_assert(VT_DECODER_MAX_AUDIO_TRACKS <= VITA_HW_DECODER_MAX_AUDIO_TRACKS &&
	           VT_DECODER_MAX_AUDIO_TRACKS <= VITA_SW_DECODER_MAX_AUDIO_TRACKS,
	           "decoder audio track capacity is smaller than the app capacity");
_Static_assert(VT_DECODER_MAX_SUBTITLE_TRACKS <=
	               VITA_HW_DECODER_MAX_SUBTITLE_TRACKS &&
	           VT_DECODER_MAX_SUBTITLE_TRACKS <=
	               VITA_SW_DECODER_MAX_SUBTITLE_TRACKS,
	           "decoder subtitle track capacity is smaller than the app capacity");
_Static_assert(VT_DECODER_AUDIO_CHANGE_ROLLED_BACK ==
	               VITA_HW_DECODER_AUDIO_CHANGE_ROLLED_BACK &&
	           VT_DECODER_AUDIO_CHANGE_ROLLED_BACK ==
	               VITA_SW_DECODER_AUDIO_CHANGE_ROLLED_BACK,
	           "decoder audio rollback result differs between backends");

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
	/* request_stop() is intentionally callable from the UI while a loading
	 * worker publishes, rejects, or replaces a backend candidate. */
	volatile int candidate_lock;
};

static void candidate_lock(VtDecoderPlayer *player) {
	while (__sync_lock_test_and_set(&player->candidate_lock, 1))
		sceKernelDelayThread(100);
}

static void candidate_unlock(VtDecoderPlayer *player) {
	__sync_lock_release(&player->candidate_lock);
}

static void publish_hardware_candidate(VtDecoderPlayer *player,
	                                   VitaHwDecoderPlayer *candidate) {
	candidate_lock(player);
	player->hardware = candidate;
	candidate_unlock(player);
}

static VitaHwDecoderPlayer *detach_hardware_candidate(
	VtDecoderPlayer *player) {
	candidate_lock(player);
	VitaHwDecoderPlayer *candidate = player->hardware;
	player->hardware = NULL;
	candidate_unlock(player);
	return candidate;
}

static void publish_software_candidate(VtDecoderPlayer *player,
	                                   VitaSwDecoderPlayer *candidate) {
	candidate_lock(player);
	player->software = candidate;
	candidate_unlock(player);
}

static VitaSwDecoderPlayer *detach_software_candidate(
	VtDecoderPlayer *player) {
	candidate_lock(player);
	VitaSwDecoderPlayer *candidate = player->software;
	player->software = NULL;
	candidate_unlock(player);
	return candidate;
}

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
	*out = (VtDecoderStreamHandle){
		.opaque = cursor,
		.read = local_read,
		.seek = local_seek,
		.abort = NULL,
		.close = local_close,
		.size = size
	};
	return 0;
}

void vt_decoder_file_stream_factory(const char *path,
	                                VtDecoderStreamFactory *factory) {
	if (!factory) return;
	memset(factory, 0, sizeof(*factory));
	factory->opaque = (void *)path;
	factory->open = local_open;
	factory->local_path = path;
}

static int hw_stream_open(void *opaque, VitaHwDecoderStreamHandle *out) {
	VtDecoderStreamFactory *factory = opaque;
	VtDecoderStreamHandle cursor = {0};
	int result = factory && factory->open ? factory->open(factory->opaque, &cursor) : -1;
	if (result < 0) return result;
	memset(out, 0, sizeof(*out));
	out->opaque = cursor.opaque;
	out->read = cursor.read;
	out->seek = cursor.seek;
	out->abort = cursor.abort;
	out->close = cursor.close;
	out->size = cursor.size;
	return 0;
}

static int hw_stream_open_with_cancel(void *opaque,
	                                  volatile int *cancel_flag,
	                                  VitaHwDecoderStreamHandle *out) {
	VtDecoderStreamFactory *factory = opaque;
	VtDecoderStreamHandle cursor = {0};
	int result = factory && factory->open_cancelable
	           ? factory->open_cancelable(factory->opaque, &cursor, cancel_flag)
	           : factory && factory->open
	           ? factory->open(factory->opaque, &cursor) : -1;
	if (result < 0) return result;
	memset(out, 0, sizeof(*out));
	out->opaque = cursor.opaque;
	out->read = cursor.read;
	out->seek = cursor.seek;
	out->abort = cursor.abort;
	out->close = cursor.close;
	out->size = cursor.size;
	return 0;
}

static int sw_stream_open(void *opaque, VitaSwDecoderStreamHandle *out) {
	VtDecoderStreamFactory *factory = opaque;
	VtDecoderStreamHandle cursor = {0};
	int result = factory && factory->open ? factory->open(factory->opaque, &cursor) : -1;
	if (result < 0) return result;
	memset(out, 0, sizeof(*out));
	out->opaque = cursor.opaque;
	out->read = cursor.read;
	out->seek = cursor.seek;
	out->abort = cursor.abort;
	out->close = cursor.close;
	out->size = cursor.size;
	return 0;
}

static int sw_stream_open_with_cancel(void *opaque,
	                                  volatile int *cancel_flag,
	                                  VitaSwDecoderStreamHandle *out) {
	VtDecoderStreamFactory *factory = opaque;
	VtDecoderStreamHandle cursor = {0};
	int result = factory && factory->open_cancelable
	           ? factory->open_cancelable(factory->opaque, &cursor, cancel_flag)
	           : factory && factory->open
	           ? factory->open(factory->opaque, &cursor) : -1;
	if (result < 0) return result;
	memset(out, 0, sizeof(*out));
	out->opaque = cursor.opaque;
	out->read = cursor.read;
	out->seek = cursor.seek;
	out->abort = cursor.abort;
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
		/* Keep the pre-reserved worker alive while subtitles are Off. Closing here
		 * discarded the early allocation and forced X selection to create a thread
		 * under peak decoder memory pressure. */
		vt_subtitle_reader_disable(player->subtitles);
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

static void copy_hw_track(VtDecoderTrackInfo *out,
	                      const VitaHwDecoderTrackInfo *source) {
	if (!out || !source) return;
	memset(out, 0, sizeof(*out));
	out->stream_index = source->stream_index;
	out->is_default = source->is_default;
	out->channels = source->channels;
	memcpy(out->language, source->language, sizeof(out->language));
	memcpy(out->title, source->title, sizeof(out->title));
	memcpy(out->codec, source->codec, sizeof(out->codec));
}

static void copy_sw_track(VtDecoderTrackInfo *out,
	                      const VitaSwDecoderTrackInfo *source) {
	if (!out || !source) return;
	memset(out, 0, sizeof(*out));
	out->stream_index = source->stream_index;
	out->is_default = source->is_default;
	out->channels = source->channels;
	memcpy(out->language, source->language, sizeof(out->language));
	memcpy(out->title, source->title, sizeof(out->title));
	memcpy(out->codec, source->codec, sizeof(out->codec));
}

static void snapshot_hardware_tracks(VtDecoderPlayer *player,
	                                 int requested_audio,
	                                 int requested_subtitle) {
	if (!player || !player->hardware) return;
	memset(player->audio_tracks, 0, sizeof(player->audio_tracks));
	memset(player->subtitle_tracks, 0, sizeof(player->subtitle_tracks));
	player->audio_track_count =
	    vita_hw_decoder_audio_track_count(player->hardware);
	if (player->audio_track_count < 0) player->audio_track_count = 0;
	if (player->audio_track_count > VT_DECODER_MAX_AUDIO_TRACKS)
		player->audio_track_count = VT_DECODER_MAX_AUDIO_TRACKS;
	for (int i = 0; i < player->audio_track_count; i++) {
		VitaHwDecoderTrackInfo info;
		if (vita_hw_decoder_audio_track_info(player->hardware, i, &info) < 0) {
			player->audio_track_count = i;
			break;
		}
		copy_hw_track(&player->audio_tracks[i], &info);
	}
	player->subtitle_track_count =
	    vita_hw_decoder_subtitle_track_count(player->hardware);
	if (player->subtitle_track_count < 0) player->subtitle_track_count = 0;
	if (player->subtitle_track_count > VT_DECODER_MAX_SUBTITLE_TRACKS)
		player->subtitle_track_count = VT_DECODER_MAX_SUBTITLE_TRACKS;
	for (int i = 0; i < player->subtitle_track_count; i++) {
		VitaHwDecoderTrackInfo info;
		if (vita_hw_decoder_subtitle_track_info(player->hardware, i, &info) < 0) {
			player->subtitle_track_count = i;
			break;
		}
		copy_hw_track(&player->subtitle_tracks[i], &info);
	}
	player->active_audio_track =
	    requested_audio >= 0 && requested_audio < player->audio_track_count
	        ? requested_audio : 0;
	player->active_subtitle_track =
	    requested_subtitle > 0 &&
	            requested_subtitle <= player->subtitle_track_count
	        ? requested_subtitle : 0;
	player->config.audio_track = player->active_audio_track;
	player->config.subtitle_track = player->active_subtitle_track;
}

static void snapshot_software_tracks(VtDecoderPlayer *player,
	                                 int requested_audio,
	                                 int requested_subtitle) {
	if (!player || !player->software) return;
	memset(player->audio_tracks, 0, sizeof(player->audio_tracks));
	memset(player->subtitle_tracks, 0, sizeof(player->subtitle_tracks));
	player->audio_track_count =
	    vita_sw_decoder_audio_track_count(player->software);
	if (player->audio_track_count < 0) player->audio_track_count = 0;
	if (player->audio_track_count > VT_DECODER_MAX_AUDIO_TRACKS)
		player->audio_track_count = VT_DECODER_MAX_AUDIO_TRACKS;
	for (int i = 0; i < player->audio_track_count; i++) {
		VitaSwDecoderTrackInfo info;
		if (vita_sw_decoder_audio_track_info(player->software, i, &info) < 0) {
			player->audio_track_count = i;
			break;
		}
		copy_sw_track(&player->audio_tracks[i], &info);
	}
	player->subtitle_track_count =
	    vita_sw_decoder_subtitle_track_count(player->software);
	if (player->subtitle_track_count < 0) player->subtitle_track_count = 0;
	if (player->subtitle_track_count > VT_DECODER_MAX_SUBTITLE_TRACKS)
		player->subtitle_track_count = VT_DECODER_MAX_SUBTITLE_TRACKS;
	for (int i = 0; i < player->subtitle_track_count; i++) {
		VitaSwDecoderTrackInfo info;
		if (vita_sw_decoder_subtitle_track_info(player->software, i, &info) < 0) {
			player->subtitle_track_count = i;
			break;
		}
		copy_sw_track(&player->subtitle_tracks[i], &info);
	}
	player->active_audio_track =
	    requested_audio >= 0 && requested_audio < player->audio_track_count
	        ? requested_audio : 0;
	player->active_subtitle_track =
	    requested_subtitle > 0 &&
	            requested_subtitle <= player->subtitle_track_count
	        ? requested_subtitle : 0;
	player->config.audio_track = player->active_audio_track;
	player->config.subtitle_track = player->active_subtitle_track;
}

int vt_decoder_open(VtDecoderPlayer *player, const VtDecoderPlayerConfig *config) {
	if (!player || !config ||
	    (!config->stream.open && !config->stream.open_cancelable)) return -1;
	player->source = config->stream;
	player->config = *config;
	memset(player->audio_tracks, 0, sizeof(player->audio_tracks));
	memset(player->subtitle_tracks, 0, sizeof(player->subtitle_tracks));
	player->audio_track_count = 0;
	player->subtitle_track_count = 0;
	player->active_audio_track = config->audio_track;
	player->active_subtitle_track = config->subtitle_track;
	uint64_t stage_started;
	VtDecoderBackend preference = config->preferred_backend;
	if (preference != VT_DECODER_BACKEND_HARDWARE &&
	    preference != VT_DECODER_BACKEND_SOFTWARE)
		preference = VT_DECODER_BACKEND_NONE;
	int result = -1;
	if (preference != VT_DECODER_BACKEND_SOFTWARE) {
		VitaHwDecoderPlayerConfig hardware = {
			.stream = {
				.opaque = &player->source,
				.open = hw_stream_open,
				.open_with_cancel = hw_stream_open_with_cancel
			},
			.audio_track = player->active_audio_track,
			.expected_width = config->expected_width,
			.expected_height = config->expected_height,
			.expected_fps = config->expected_fps,
			.start_position_ms = config->start_position_ms,
			.volume_percent = config->volume_percent,
			.cancel_flag = config->cancel_flag
		};
		VitaHwDecoderPlayer *hardware_candidate = vita_hw_decoder_create();
		publish_hardware_candidate(player, hardware_candidate);
		stage_started = sceKernelGetProcessTimeWide();
		result = hardware_candidate
		       ? vita_hw_decoder_open(hardware_candidate, &hardware) : -1;
		log_printf("decoder open: hardware=%llu us ret=%d\n",
		           (unsigned long long)(sceKernelGetProcessTimeWide() -
		                                stage_started),
		           result);
		if (result >= 0) {
			player->backend = VT_DECODER_BACKEND_HARDWARE;
			snapshot_hardware_tracks(player, config->audio_track,
			                         config->subtitle_track);
			log_printf("decoder tracks: reused hardware demux audio=%d subtitles=%d\n",
			           player->audio_track_count, player->subtitle_track_count);
			open_selected_subtitles(player, config->start_position_ms);
			return 0;
		}
		/* AVERROR_EXIT can also be an internal FFmpeg wall deadline. Only the
		 * caller-owned token proves that the user cancelled AUTO startup; otherwise
		 * software fallback must still be attempted. */
		int cancelled = config->cancel_flag && *config->cancel_flag;
		hardware_candidate = detach_hardware_candidate(player);
		if (hardware_candidate) vita_hw_decoder_destroy(hardware_candidate);
		/* A user-cancelled hardware open is terminal. Falling through to software
		 * would clear the flag inside the second backend and make Circle appear to
		 * have been ignored. */
		if (cancelled) return result < 0 ? result : AVERROR_EXIT;
		if (preference == VT_DECODER_BACKEND_HARDWARE) return result;
	}
	/* The source contract creates fresh independent cursors, so fallback can
	 * safely reopen after a failed hardware session. A software preference
	 * arrives here directly without initializing the hardware backend. */
	VitaSwDecoderPlayerConfig software = {
		.stream = {
			.opaque = &player->source,
			.open = sw_stream_open,
			.open_with_cancel = sw_stream_open_with_cancel
		},
		.audio_track = player->active_audio_track,
		.expected_width = config->expected_width,
		.expected_height = config->expected_height,
		.expected_fps = config->expected_fps,
		.start_position_ms = config->start_position_ms,
		.volume_percent = config->volume_percent,
		.cancel_flag = config->cancel_flag
	};
	VitaSwDecoderPlayer *software_candidate = vita_sw_decoder_create();
	publish_software_candidate(player, software_candidate);
	stage_started = sceKernelGetProcessTimeWide();
	result = software_candidate
	       ? vita_sw_decoder_open(software_candidate, &software) : -1;
	log_printf("decoder open: software=%llu us ret=%d\n",
	           (unsigned long long)(sceKernelGetProcessTimeWide() -
	                                stage_started),
	           result);
	if (result >= 0) {
		player->backend = VT_DECODER_BACKEND_SOFTWARE;
		snapshot_software_tracks(player, config->audio_track,
		                         config->subtitle_track);
		log_printf("decoder tracks: reused software demux audio=%d subtitles=%d\n",
		           player->audio_track_count, player->subtitle_track_count);
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
	VitaHwDecoderPlayer *hardware_candidate =
		detach_hardware_candidate(player);
	if (hardware_candidate) vita_hw_decoder_destroy(hardware_candidate);
	VitaSwDecoderPlayerConfig software = {
		.stream = {
			.opaque = &player->source,
			.open = sw_stream_open,
			.open_with_cancel = sw_stream_open_with_cancel
		},
		.audio_track = player->active_audio_track,
		.expected_width = player->config.expected_width,
		.expected_height = player->config.expected_height,
		.expected_fps = player->config.expected_fps,
		.start_position_ms = position_ms,
		.volume_percent = player->config.volume_percent,
		.cancel_flag = player->config.cancel_flag
	};
	VitaSwDecoderPlayer *software_candidate = vita_sw_decoder_create();
	publish_software_candidate(player, software_candidate);
	uint64_t stage_started = sceKernelGetProcessTimeWide();
	int result = software_candidate
	           ? vita_sw_decoder_open(software_candidate, &software) : -1;
	log_printf("decoder fallback: software=%llu us ret=%d\n",
	           (unsigned long long)(sceKernelGetProcessTimeWide() -
	                                stage_started),
	           result);
	player->backend = result >= 0
	                ? VT_DECODER_BACKEND_SOFTWARE : VT_DECODER_BACKEND_NONE;
	if (result >= 0)
		snapshot_software_tracks(player, player->active_audio_track,
		                         player->active_subtitle_track);
	return result;
}

void vt_decoder_destroy(VtDecoderPlayer *player) {
	if (!player) return;
	vt_subtitle_reader_destroy(player->subtitles);
	VitaHwDecoderPlayer *hardware = detach_hardware_candidate(player);
	VitaSwDecoderPlayer *software = detach_software_candidate(player);
	if (hardware) vita_hw_decoder_destroy(hardware);
	if (software) vita_sw_decoder_destroy(software);
	free(player);
}

void vt_decoder_request_stop(VtDecoderPlayer *player) {
	if (!player) return;
	/* Open/fallback can be in progress before backend is published, so interrupt
	 * every allocated candidate rather than dispatching only on backend.  Keep
	 * the publication lock through each nonblocking signal so a loading worker
	 * cannot detach/free the candidate between the pointer test and the call. */
	candidate_lock(player);
	if (player->hardware) vita_hw_decoder_request_stop(player->hardware);
	if (player->software) vita_sw_decoder_request_stop(player->software);
	candidate_unlock(player);
}

void vt_decoder_prepare_background_restart(VtDecoderPlayer *player) {
	if (!player) return;
	/* Fallback can own an allocated backend before/after the public enum changes;
	 * mark every live candidate just as request_stop() does. */
	candidate_lock(player);
	if (player->hardware)
		vita_hw_decoder_prepare_background_restart(player->hardware);
	if (player->software)
		vita_sw_decoder_prepare_background_restart(player->software);
	candidate_unlock(player);
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
	VtDecoderBackend attempted_backend = player->backend;
	int result = player->backend == VT_DECODER_BACKEND_HARDWARE
	     ? vita_hw_decoder_seek(player->hardware, position_ms)
	     : player->backend == VT_DECODER_BACKEND_SOFTWARE
	     ? vita_sw_decoder_seek(player->software, position_ms) : -1;
	if (result < 0 && attempted_backend == VT_DECODER_BACKEND_HARDWARE &&
	    player->config.preferred_backend == VT_DECODER_BACKEND_NONE &&
	    !(player->config.cancel_flag && *player->config.cancel_flag)) {
		log_printf("decoder seek: hardware ret=%d; reopening software at %llu ms\n",
		           result, (unsigned long long)position_ms);
		result = vt_decoder_fallback_to_software(player, position_ms);
	}
	if (result >= 0) open_selected_subtitles(player, position_ms);
	return result;
}

int vt_decoder_select_audio_track_with_cancel(
	VtDecoderPlayer *player, int audio_track, uint64_t position_ms,
	volatile int *operation_cancel) {
	if (!player || audio_track < 0 || audio_track >= player->audio_track_count)
		return -1;
	if (audio_track == player->active_audio_track) return 0;
	int result = player->backend == VT_DECODER_BACKEND_HARDWARE
	           ? vita_hw_decoder_select_audio_track_with_cancel(
	                 player->hardware, audio_track, position_ms,
	                 operation_cancel)
	           : player->backend == VT_DECODER_BACKEND_SOFTWARE
	           ? vita_sw_decoder_select_audio_track_with_cancel(
	                 player->software, audio_track, position_ms,
	                 operation_cancel)
	           : -1;
	if (result == 0) {
		player->active_audio_track = audio_track;
		player->config.audio_track = audio_track;
	} else if (result == VT_DECODER_AUDIO_CHANGE_ROLLED_BACK) {
		log_printf("audio track change rolled back; active=%d requested=%d\n",
		           player->active_audio_track, audio_track);
	}
	return result;
}

int vt_decoder_select_audio_track(VtDecoderPlayer *player, int audio_track,
	                              uint64_t position_ms) {
	return vt_decoder_select_audio_track_with_cancel(
	    player, audio_track, position_ms, NULL);
}

void vt_decoder_interrupt_audio_operation(VtDecoderPlayer *player) {
	if (!player) return;
	if (player->backend == VT_DECODER_BACKEND_HARDWARE && player->hardware)
		vita_hw_decoder_interrupt_audio_operation(player->hardware);
	else if (player->backend == VT_DECODER_BACKEND_SOFTWARE && player->software)
		vita_sw_decoder_interrupt_audio_operation(player->software);
}

int vt_decoder_select_subtitle_track(VtDecoderPlayer *player,
	                                 int subtitle_track,
	                                 uint64_t position_ms) {
	if (!player || subtitle_track < 0 ||
	    subtitle_track > player->subtitle_track_count) return -1;
	VtSubtitleReaderState reader_state =
	    vt_subtitle_reader_state(player->subtitles);
	if (subtitle_track == player->active_subtitle_track &&
	    reader_state != VT_SUBTITLE_READER_PENDING &&
	    reader_state != VT_SUBTITLE_READER_FAILED) return 0;
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
	    NULL);
	/* Keep the user's requested track selected even if worker creation fails.
	 * The reader exposes FAILED explicitly; silently rewriting the selection to
	 * Off hid the error and made retrying the same track impossible. */
	player->active_subtitle_track = subtitle_track;
	player->config.subtitle_track = subtitle_track;
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

VtDecoderSubtitleState vt_decoder_subtitle_state(const VtDecoderPlayer *player) {
	if (!player) return VT_DECODER_SUBTITLE_DISABLED;
	return (VtDecoderSubtitleState)vt_subtitle_reader_state(player->subtitles);
}

int vt_decoder_subtitle_error(const VtDecoderPlayer *player) {
	return player ? vt_subtitle_reader_error(player->subtitles) : 0;
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

void vt_decoder_discard_video_to_clock(VtDecoderPlayer *player) {
	if (!player) return;
	if (player->backend == VT_DECODER_BACKEND_HARDWARE && player->hardware)
		vita_hw_decoder_discard_video_to_clock(player->hardware);
	else if (player->backend == VT_DECODER_BACKEND_SOFTWARE && player->software)
		vita_sw_decoder_discard_video_to_clock(player->software);
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
	/* Status is sampled every player frame, including OLED eco mode where text
	 * rendering can be skipped. Keep the asynchronous subtitle deadline moving
	 * without making the drawer or Circle wait on the subtitle worker. */
	vt_subtitle_reader_tick(player->subtitles);
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
