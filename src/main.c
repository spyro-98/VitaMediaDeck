#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include "app_paths.h"
#include "common/text_log.h"
#include "history/playback_history.h"
#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "media/hw_player_screen.h"
#include "media/video_thumbnail.h"
#include "network/network_source.h"
#include "settings/preferences.h"
#include "ui/about_screen.h"
#include "ui/loading_screen.h"
#include "ui/local_files_screen.h"
#include "ui/local_media_screen.h"
#include "ui/mini_player.h"
#include "ui/music_player.h"
#include "ui/network_sources_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/settings_screen.h"
#include "ui/text_input.h"
#include "ui/touch.h"

#include <vita_https.h>

static int g_network_ready;

typedef struct {
	int valid;
	VtLocalMediaItem item;
	int audio_track;
	int subtitle_track;
} VtMinimizedLocalVideo;

typedef struct {
	int valid;
	VtLocalMediaItem item;
} VtMinimizedLocalAudio;

typedef struct {
	int valid;
	UiNetworkSelection selection;
	int audio_track;
	int subtitle_track;
} VtMinimizedRemoteVideo;

static VtMinimizedLocalVideo g_minimized_local_video;
static VtMinimizedLocalAudio g_minimized_local_audio;
static VtMinimizedRemoteVideo g_minimized_remote_video;

static int resume_minimized_local_video(uint64_t position_ms, void *ctx);
static int resume_minimized_local_audio(uint64_t position_ms, void *ctx);
static int resume_minimized_remote_video(uint64_t position_ms, void *ctx);

static void clear_minimized_remote_video(void) {
	memset(&g_minimized_remote_video, 0, sizeof(g_minimized_remote_video));
}

static void media_id(const char *path, char out[16]) {
	uint32_t hash = 2166136261U;
	for (const unsigned char *cursor = (const unsigned char *)path;
	     cursor && *cursor; cursor++) {
		hash ^= *cursor;
		hash *= 16777619U;
	}
	snprintf(out, 16, "media%08x", hash);
}

static uint64_t history_hash_text(uint64_t hash, const char *text) {
	for (const unsigned char *cursor = (const unsigned char *)text;
	     cursor && *cursor; cursor++) {
		hash ^= *cursor;
		hash *= 1099511628211ULL;
	}
	hash ^= 0xffU;
	return hash * 1099511628211ULL;
}

static uint64_t history_hash_u64(uint64_t hash, uint64_t value) {
	for (unsigned int shift = 0; shift < 64; shift += 8) {
		hash ^= (unsigned char)(value >> shift);
		hash *= 1099511628211ULL;
	}
	return hash;
}

static void remote_media_id(const UiNetworkSelection *selection, char out[16]) {
	uint64_t hash = 14695981039346656037ULL;
	if (selection) {
		hash = history_hash_u64(hash, selection->source.protocol);
		hash = history_hash_text(hash, selection->source.host);
		hash = history_hash_u64(hash, selection->source.port);
		hash = history_hash_text(hash, selection->source.username);
		hash = history_hash_text(hash, selection->source.domain);
		hash = history_hash_text(hash, selection->source.root_path);
		hash = history_hash_text(hash, selection->source.share);
		hash = history_hash_text(hash, selection->path);
	}
	snprintf(out, 16, "r%014llx",
	         (unsigned long long)(hash & 0x00ffffffffffffffULL));
}

static int valid_filename(const char *name) {
	if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, "..")) return 0;
	for (const unsigned char *cursor = (const unsigned char *)name; *cursor; cursor++)
		if (*cursor == '/' || *cursor == '\\' || *cursor == ':') return 0;
	return 1;
}

static int sidecar_path(const char *media, const char *suffix,
	                    char *out, size_t out_size) {
	int length = snprintf(out, out_size, "%s", media ? media : "");
	if (length <= 0 || length >= (int)out_size) return -1;
	char *dot = strrchr(out, '.');
	if (!dot) return -1;
	size_t remaining = out_size - (size_t)(dot - out);
	length = snprintf(dot, remaining, "%s", suffix);
	return length > 0 && length < (int)remaining ? 0 : -1;
}

static int rename_local_media(VtLocalMediaItem *item) {
	if (!item) return -1;
	char name[VT_LOCAL_MEDIA_NAME_MAX];
	snprintf(name, sizeof(name), "%s", item->name);
	if (ui_text_input(vt_i18n_str(VT_STR_MAIN_RENAME_MEDIA),
	                  name, name, sizeof(name)) <= 0) return 1;
	if (!valid_filename(name)) return -1;
	const char *slash = strrchr(item->path, '/');
	if (!slash) return -1;
	char target[VT_LOCAL_MEDIA_PATH_MAX];
	int directory_length = (int)(slash - item->path + 1);
	if (directory_length <= 0 || directory_length >= (int)sizeof(target)) return -1;
	memcpy(target, item->path, (size_t)directory_length);
	target[directory_length] = '\0';
	const char *old_extension = strrchr(item->path, '.');
	const char *new_extension = strrchr(name, '.');
	int written = snprintf(target + directory_length,
	                       sizeof(target) - (size_t)directory_length,
	                       "%s%s", name,
	                       (!new_extension && old_extension) ? old_extension : "");
	if (written <= 0 || written >= (int)(sizeof(target) - (size_t)directory_length))
		return -1;
	SceIoStat collision;
	if (sceIoGetstat(target, &collision) >= 0) return -1;
	int ret = sceIoRename(item->path, target);
	if (ret < 0) return ret;
	static const char *const suffixes[] = {
		".jpg", ".jpeg", ".png", ".meta", ".srt", ".vtt"
	};
	for (unsigned int i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
		char from[VT_LOCAL_MEDIA_PATH_MAX], to[VT_LOCAL_MEDIA_PATH_MAX];
		if (sidecar_path(item->path, suffixes[i], from, sizeof(from)) == 0 &&
		    sidecar_path(target, suffixes[i], to, sizeof(to)) == 0)
			sceIoRename(from, to);
	}
	return 0;
}

static int delete_local_media(const VtLocalMediaItem *item) {
	if (!item) return -1;
	int ret = sceIoRemove(item->path);
	if (ret < 0) return ret;
	static const char *const suffixes[] = {
		".jpg", ".jpeg", ".png", ".meta", ".srt", ".vtt"
	};
	for (unsigned int i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
		char path[VT_LOCAL_MEDIA_PATH_MAX];
		if (sidecar_path(item->path, suffixes[i], path, sizeof(path)) == 0)
			sceIoRemove(path);
	}
	return 0;
}

static int active_media_id_matches(const char *id) {
	VtBackgroundPlaybackSnapshot snapshot;
	return id && id[0] && vt_background_playback_can_resume_fullscreen() &&
	       vt_background_playback_snapshot(&snapshot) &&
	       strcmp(snapshot.video_id, id) == 0;
}

static int active_media_matches(const char *path) {
	char id[16];
	media_id(path, id);
	return active_media_id_matches(id);
}

static void remember_minimized_local_audio(const VtLocalMediaItem *item) {
	if (!item) return;
	g_minimized_local_audio.valid = 1;
	g_minimized_local_audio.item = *item;
	vt_background_playback_set_fullscreen_resume(
	    resume_minimized_local_audio, &g_minimized_local_audio);
}

static int run_local_audio_session(VtLocalMediaItem item, int prepare_first,
	                               int *navigated) {
	if (navigated) *navigated = 0;
	memset(&g_minimized_local_video, 0, sizeof(g_minimized_local_video));
	for (;;) {
		vt_background_playback_set_fullscreen_resume(NULL, NULL);
		if (prepare_first) {
			clear_minimized_remote_video();
			char id[16];
			media_id(item.path, id);
			vt_background_playback_request_stop();
			int ret = vt_background_playback_prepare_local(
			    item.path, id, item.name, item.artist,
			    item.artwork_path[0] ? item.artwork_path : NULL, item.duration_ms);
			if (ret == 0) ret = vt_background_playback_activate(0);
			if (ret < 0) {
				ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
				                vt_i18n_str(VT_STR_MAIN_AUDIO_OPEN_FAILED), 2800);
				memset(&g_minimized_local_audio, 0,
				       sizeof(g_minimized_local_audio));
				return UI_SECTION_LOCAL_MEDIA;
			}
		}
		uint32_t bitrate = item.duration_ms && item.size <= UINT64_MAX / 8ULL
		                   ? (uint32_t)((item.size * 8ULL) / item.duration_ms) : 0;
		int action = ui_music_player_run(item.artwork_path, item.album, bitrate);
		if (action >= UI_MUSIC_PLAYER_SECTION_BASE) {
			remember_minimized_local_audio(&item);
			if (navigated) *navigated = 1;
			return action - UI_MUSIC_PLAYER_SECTION_BASE;
		}
		if (action == UI_MUSIC_PLAYER_MINIMIZE) {
			remember_minimized_local_audio(&item);
			return UI_SECTION_LOCAL_MEDIA;
		}
		if (action == UI_MUSIC_PLAYER_STOP) {
			memset(&g_minimized_local_audio, 0,
			       sizeof(g_minimized_local_audio));
			vt_background_playback_set_fullscreen_resume(NULL, NULL);
			return UI_SECTION_LOCAL_MEDIA;
		}
		VtLocalMediaItem next;
		if (action != UI_MUSIC_PLAYER_REPEAT &&
		    ui_local_media_next_audio(item.path, 1,
		                              ui_music_player_shuffle_enabled(), &next) < 0)
			return UI_SECTION_LOCAL_MEDIA;
		if (action != UI_MUSIC_PLAYER_REPEAT) item = next;
		prepare_first = 1;
	}
}

static int resume_minimized_local_audio(uint64_t position_ms, void *ctx) {
	(void)position_ms;
	VtMinimizedLocalAudio *session = ctx;
	if (!session || !session->valid) return -1;
	VtLocalMediaItem item = session->item;
	int navigated = 0;
	int section = run_local_audio_session(item, 0, &navigated);
	return navigated ? VT_HW_PLAYER_ACTION_SECTION_BASE + section : 0;
}

static int play_local_audio(VtLocalMediaItem item) {
	if (active_media_matches(item.path)) return UI_SECTION_PLAYER;
	int navigated = 0;
	return run_local_audio_session(item, 1, &navigated);
}

static int run_local_video_fullscreen(const VtLocalMediaItem *item,
	                                  uint64_t start_position_ms,
	                                  int start_audio_track,
	                                  int start_subtitle_track,
	                                  uint64_t *last_position_ms,
	                                  uint64_t *last_duration_ms,
	                                  int *last_audio_track,
	                                  int *last_subtitle_track) {
	if (!item) return -1;
	vt_video_thumbnail_prepare_playback();
	char id[16];
	media_id(item->path, id);
	VtDecoderStreamFactory factory;
	vt_decoder_file_stream_factory(item->path, &factory);
	VtHwPlayerScreenSource source = {
		.stream = factory,
		.title = item->name,
		.location = vt_i18n_str(VT_STR_LOCAL_MEDIA_TITLE),
		.history_id = id,
		.authenticated_remote = 0,
		.allow_minimize = 1,
		.start_position_ms = start_position_ms,
		.start_audio_track = start_audio_track,
		.start_subtitle_track = start_subtitle_track,
		.expected_width = 0,
		.expected_height = 0,
		.expected_fps = 0
	};
	return vt_hw_player_screen_run(&source, last_position_ms, last_duration_ms,
	                               last_audio_track, last_subtitle_track);
}

static int minimize_local_video(const VtLocalMediaItem *item,
	                            uint64_t position_ms,
	                            int audio_track,
	                            int subtitle_track) {
	if (!item) return -1;
	char id[16];
	media_id(item->path, id);
	int ret = vt_background_playback_prepare_local_video(
	    item->path, id, item->name,
	    item->artist[0] ? item->artist : vt_i18n_str(VT_STR_LOCAL_MEDIA_TITLE),
	    item->artwork_path[0] ? item->artwork_path : NULL,
	    item->duration_ms, audio_track);
	if (ret < 0) return ret;
	g_minimized_local_video.valid = 1;
	g_minimized_local_video.item = *item;
	g_minimized_local_video.audio_track = audio_track;
	g_minimized_local_video.subtitle_track = subtitle_track;
	vt_background_playback_set_fullscreen_resume(
	    resume_minimized_local_video, &g_minimized_local_video);
	ret = vt_background_playback_activate(position_ms);
	if (ret < 0) {
		vt_background_playback_set_fullscreen_resume(NULL, NULL);
		vt_background_playback_stop();
		memset(&g_minimized_local_video, 0, sizeof(g_minimized_local_video));
	}
	return ret;
}

static int resume_minimized_local_video(uint64_t position_ms, void *ctx) {
	VtMinimizedLocalVideo *session = ctx;
	if (!session || !session->valid) return -1;
	VtLocalMediaItem item = session->item;
	int audio_track = session->audio_track;
	int subtitle_track = session->subtitle_track;
	vt_background_playback_stop();
	uint64_t last_position = position_ms;
	uint64_t last_duration = item.duration_ms;
	int last_audio_track = audio_track;
	int last_subtitle_track = subtitle_track;
	int ret = run_local_video_fullscreen(
	    &item, position_ms, audio_track, subtitle_track, &last_position,
	    &last_duration, &last_audio_track, &last_subtitle_track);
	char id[16];
	media_id(item.path, id);
	vt_playback_history_update(id, last_position,
	                           last_duration ? last_duration : item.duration_ms);
	if (ret == VT_HW_PLAYER_ACTION_MINIMIZE) {
		if (minimize_local_video(&item, last_position, last_audio_track,
		                         last_subtitle_track) < 0) {
			ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
			                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
			return -1;
		}
		return 0;
	}
	if (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
	    ret < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_COUNT) {
		if (minimize_local_video(&item, last_position, last_audio_track,
		                         last_subtitle_track) < 0) {
			ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
			                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
			return -1;
		}
		return ret;
	}
	vt_background_playback_set_fullscreen_resume(NULL, NULL);
	memset(&g_minimized_local_video, 0, sizeof(g_minimized_local_video));
	return ret;
}

static int play_local_video(const VtLocalMediaItem *item) {
	if (active_media_matches(item->path)) return UI_SECTION_PLAYER;
	clear_minimized_remote_video();
	char id[16];
	media_id(item->path, id);
	vt_background_playback_set_fullscreen_resume(NULL, NULL);
	vt_background_playback_stop();
	uint64_t last_position = vt_playback_history_position(id, item->duration_ms);
	uint64_t last_duration = item->duration_ms;
	int last_audio_track = 0;
	int last_subtitle_track = 0;
	int ret = run_local_video_fullscreen(item, last_position, 0, 0,
	                                    &last_position, &last_duration,
	                                    &last_audio_track,
	                                    &last_subtitle_track);
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	vt_playback_history_update(id, last_position,
	                           last_duration ? last_duration : item->duration_ms);
	if (ret == VT_HW_PLAYER_ACTION_MINIMIZE) {
		if (minimize_local_video(item, last_position, last_audio_track,
		                         last_subtitle_track) < 0)
			ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
			                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
		return UI_SECTION_LOCAL_MEDIA;
	}
	if (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
	    ret < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_COUNT) {
		if (minimize_local_video(item, last_position, last_audio_track,
		                         last_subtitle_track) < 0)
			ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
			                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
		return ret - VT_HW_PLAYER_ACTION_SECTION_BASE;
	}
	if (ret < 0)
		ui_message_show(vt_i18n_str(VT_STR_MAIN_UNSUPPORTED_MEDIA),
		                vt_i18n_str(VT_STR_MAIN_UNSUPPORTED_DETAIL), 3200);
	return UI_SECTION_LOCAL_MEDIA;
}

static int run_remote_video_fullscreen(
	const UiNetworkSelection *selection,
	uint64_t start_position_ms,
	int start_audio_track,
	int start_subtitle_track,
	uint64_t *last_position_ms,
	uint64_t *last_duration_ms,
	int *last_audio_track,
	int *last_subtitle_track) {
	if (!selection) return -1;
	vt_video_thumbnail_prepare_playback();
	VtNetworkStreamFactory remote;
	VtNetworkStreamFactory subtitle_factories[VT_JELLYFIN_MAX_EXTERNAL_SUBTITLES];
	memset(subtitle_factories, 0, sizeof(subtitle_factories));
	int ret = vt_network_stream_factory_init(&remote, &selection->source,
	                                         &selection->credential,
	                                         selection->path);
	if (ret < 0) return ret;
	char id[16];
	remote_media_id(selection, id);
	VtHwPlayerScreenSource source = {
		.stream = remote.factory,
		.title = selection->title,
		.location = selection->source.name,
		.history_id = id,
		.authenticated_remote = 1,
		.allow_minimize = 1,
		.start_position_ms = start_position_ms,
		.start_audio_track = start_audio_track,
		.start_subtitle_track = start_subtitle_track
	};
	if (selection->has_jellyfin_metadata &&
	    selection->jellyfin_metadata.media_source_id[0]) {
		int count = selection->jellyfin_metadata.external_subtitle_count;
		if (count > VT_JELLYFIN_MAX_EXTERNAL_SUBTITLES)
			count = VT_JELLYFIN_MAX_EXTERNAL_SUBTITLES;
		for (int i = 0; i < count; i++) {
			const VtJellyfinSubtitleTrack *track =
			    &selection->jellyfin_metadata.external_subtitles[i];
			if (vt_network_jellyfin_subtitle_stream_factory_init(
			        &subtitle_factories[source.external_subtitle_count],
			        &selection->source, &selection->credential, selection->path,
			        selection->jellyfin_metadata.media_source_id,
			        track->stream_index) < 0)
				continue;
			VtDecoderExternalSubtitle *external =
			    &source.external_subtitles[source.external_subtitle_count++];
			external->stream =
			    subtitle_factories[source.external_subtitle_count - 1].factory;
			external->info.stream_index = 0;
			external->info.is_default = track->is_default;
			snprintf(external->info.language, sizeof(external->info.language),
			         "%s", track->language);
			snprintf(external->info.title, sizeof(external->info.title), "%s",
			         track->title);
			snprintf(external->info.codec, sizeof(external->info.codec), "%s",
			         track->codec[0] ? track->codec : "subrip");
		}
	}
	ret = vt_hw_player_screen_run(&source, last_position_ms,
	                              last_duration_ms, last_audio_track,
	                              last_subtitle_track);
	memset(&remote.credential, 0, sizeof(remote.credential));
	for (int i = 0; i < source.external_subtitle_count; i++)
		memset(&subtitle_factories[i].credential, 0,
		       sizeof(subtitle_factories[i].credential));
	return ret;
}

static int minimize_remote_video(const UiNetworkSelection *selection,
	                             uint64_t position_ms,
	                             uint64_t duration_ms,
	                             int audio_track,
	                             int subtitle_track) {
	if (!selection) return -1;
	char id[16];
	remote_media_id(selection, id);
	int ret = vt_background_playback_prepare_remote_video(
	    &selection->source, &selection->credential, selection->path, id,
	    selection->title, selection->source.name, duration_ms, audio_track);
	if (ret < 0) return ret;
	g_minimized_remote_video.valid = 1;
	g_minimized_remote_video.selection = *selection;
	g_minimized_remote_video.audio_track = audio_track;
	g_minimized_remote_video.subtitle_track = subtitle_track;
	vt_background_playback_set_fullscreen_resume(
	    resume_minimized_remote_video, &g_minimized_remote_video);
	ret = vt_background_playback_activate(position_ms);
	if (ret < 0) {
		vt_background_playback_set_fullscreen_resume(NULL, NULL);
		vt_background_playback_stop();
		clear_minimized_remote_video();
	}
	return ret;
}

static int resume_minimized_remote_video(uint64_t position_ms, void *ctx) {
	VtMinimizedRemoteVideo *session = ctx;
	if (!session || !session->valid) return -1;
	UiNetworkSelection selection = session->selection;
	int audio_track = session->audio_track;
	int subtitle_track = session->subtitle_track;
	vt_background_playback_stop();
	uint64_t last_position = position_ms;
	uint64_t last_duration = 0;
	int last_audio_track = audio_track;
	int last_subtitle_track = subtitle_track;
	int ret = run_remote_video_fullscreen(
	    &selection, position_ms, audio_track, subtitle_track,
	    &last_position, &last_duration, &last_audio_track,
	    &last_subtitle_track);
	char id[16];
	remote_media_id(&selection, id);
	vt_playback_history_update(id, last_position, last_duration);
	if (ret == VT_HW_PLAYER_ACTION_MINIMIZE ||
	    (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
	     ret < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_COUNT)) {
		if (minimize_remote_video(&selection, last_position, last_duration,
		                          last_audio_track, last_subtitle_track) < 0) {
			ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
			                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
			memset(&selection.credential, 0, sizeof(selection.credential));
			return -1;
		}
		memset(&selection.credential, 0, sizeof(selection.credential));
		return ret == VT_HW_PLAYER_ACTION_MINIMIZE ? 0 : ret;
	}
	vt_background_playback_set_fullscreen_resume(NULL, NULL);
	clear_minimized_remote_video();
	memset(&selection.credential, 0, sizeof(selection.credential));
	return ret;
}

static int browse_local(void) {
	int folder_browser = 0;
	for (;;) {
		VtLocalMediaItem item;
		int action = folder_browser ? ui_local_files_screen(&item)
		                           : ui_local_media_screen(&item);
		if (action >= UI_LOCAL_MEDIA_ACTION_SECTION_BASE)
			return action - UI_LOCAL_MEDIA_ACTION_SECTION_BASE;
		if (action == UI_LOCAL_MEDIA_ACTION_BACK) {
			if (folder_browser) { folder_browser = 0; continue; }
			return UI_SECTION_LOCAL_MEDIA;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_BROWSE_FILES) {
			folder_browser = 1;
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_RENAME) {
			if (rename_local_media(&item) < 0)
				ui_message_show(vt_i18n_str(VT_STR_MAIN_RENAME_FAILED),
				                vt_i18n_str(VT_STR_MAIN_RENAME_UNCHANGED), 2400);
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_DELETE) {
			if (delete_local_media(&item) < 0)
				ui_message_show(vt_i18n_str(VT_STR_MAIN_DELETE_FAILED),
				                vt_i18n_str(VT_STR_MAIN_DELETE_UNCHANGED), 2400);
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_PLAY) {
			if (item.type == VT_LOCAL_MEDIA_AUDIO) {
				int section = play_local_audio(item);
				if (section != UI_SECTION_LOCAL_MEDIA) return section;
			} else {
				int section = play_local_video(&item);
				if (section != UI_SECTION_LOCAL_MEDIA) return section;
			}
		}
	}
}

static int browse_network(void) {
	if (!g_network_ready) {
		ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
		                vt_i18n_str(VT_STR_MAIN_LOCAL_AVAILABLE), 2600);
		return UI_SECTION_NETWORK;
	}
	for (;;) {
		UiNetworkSelection selection;
		int action = ui_network_sources_screen(&selection);
		if (action >= UI_NETWORK_ACTION_SECTION_BASE)
			return action - UI_NETWORK_ACTION_SECTION_BASE;
		if (action != UI_NETWORK_ACTION_PLAY) return UI_SECTION_NETWORK;
		char id[16];
		remote_media_id(&selection, id);
		if (active_media_id_matches(id)) {
			/* Selecting the same remote cell is a restore action, exactly like the
			 * local grid. Preserve the live mini-player position and chosen tracks. */
			memset(&selection.credential, 0, sizeof(selection.credential));
			return UI_SECTION_PLAYER;
		}
		uint64_t last_position = vt_playback_history_position(id, 0);
		uint64_t last_duration = 0;
		vt_background_playback_stop();
		clear_minimized_remote_video();
		int last_audio_track = 0;
		int last_subtitle_track = 0;
		int ret = run_remote_video_fullscreen(
		    &selection, last_position, 0, 0, &last_position,
		    &last_duration, &last_audio_track, &last_subtitle_track);
		log_save(VITAMEDIADECK_SESSION_LOG_PATH);
		vt_playback_history_update(id, last_position, last_duration);
		if (ret == VT_HW_PLAYER_ACTION_MINIMIZE ||
		    (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
		     ret < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_COUNT)) {
			if (minimize_remote_video(&selection, last_position, last_duration,
			                          last_audio_track, last_subtitle_track) < 0)
				ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
				                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
			memset(&selection.credential, 0, sizeof(selection.credential));
			if (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE)
				return ret - VT_HW_PLAYER_ACTION_SECTION_BASE;
			continue;
		}
		memset(&selection.credential, 0, sizeof(selection.credential));
		if (ret < 0)
			ui_message_show(vt_i18n_str(VT_STR_MAIN_STREAMING_FAILED),
			                vt_i18n_str(VT_STR_MAIN_STREAMING_DETAIL), 3200);
	}
}

static int run_application(void) {
	vt_background_playback_init();
	vt_playback_history_init();
	g_network_ready = vita_https_init() >= 0;
	if (g_network_ready) g_network_ready = vt_network_init() >= 0;
	if (!vt_preferences_startup_controls_seen()) {
		ui_settings_show_controls_reference();
		vt_preferences_set_startup_controls_seen(1);
		ui_touch_reset();
	}
	int section = UI_SECTION_LOCAL_MEDIA;
	int return_section = UI_SECTION_LOCAL_MEDIA;
	for (;;) {
		if (section == UI_SECTION_PLAYER) {
			int action = vt_background_playback_resume_fullscreen();
			section = action >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
			          action < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_COUNT
			        ? action - VT_HW_PLAYER_ACTION_SECTION_BASE : return_section;
			continue;
		}
		return_section = section;
		if (section == UI_SECTION_LOCAL_MEDIA) section = browse_local();
		else if (section == UI_SECTION_NETWORK) {
			if (!g_network_ready) {
				ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
				                vt_i18n_str(VT_STR_MAIN_LOCAL_AVAILABLE), 2600);
				section = UI_SECTION_LOCAL_MEDIA;
			} else section = browse_network();
		} else if (section == UI_SECTION_SETTINGS) {
			int next = ui_settings_screen();
			section = next == UI_SECTION_NONE ? UI_SECTION_LOCAL_MEDIA : next;
		} else if (section == UI_SECTION_INFO) {
			int next = ui_about_screen(0);
			section = next == UI_SECTION_NONE ? UI_SECTION_LOCAL_MEDIA : next;
		} else section = UI_SECTION_LOCAL_MEDIA;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	log_init();
	vt_preferences_init();
	vt_i18n_init();
	if (ui_runtime_init() < 0) {
		sceKernelExitProcess(0);
		return 0;
	}
	ui_loading_present(NULL);
	ui_runtime_load_assets();
	ui_touch_init();
	int thumbnail_ret = vt_video_thumbnail_init();
	if (thumbnail_ret < 0)
		log_printf("video thumbnail worker unavailable: 0x%08X",
		           (unsigned)thumbnail_ret);
	run_application();
	vt_video_thumbnail_shutdown();
	vt_background_playback_shutdown();
	ui_mini_player_shutdown();
	vt_network_shutdown();
	vita_https_shutdown();
	ui_touch_term();
	ui_runtime_term();
	sceKernelExitProcess(0);
	return 0;
}
