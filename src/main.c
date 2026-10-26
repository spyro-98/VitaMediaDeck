#include <psp2/ctrl.h>
#include "library/catalog.h"
#include "library/search.h"
#include "ui/library_screen.h"
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
#include "media/podcast_metadata.h"
#include "network/download_manager.h"
#include "network/network_source.h"
#include "settings/preferences.h"
#include "ui/about_screen.h"
#include "ui/loading_screen.h"
#include "ui/home_tabs.h"
#include "ui/image_viewer.h"
#include "ui/local_files_screen.h"
#include "ui/folder_picker.h"
#include "ui/local_media_actions.h"
#include "ui/local_media_screen.h"
#include "ui/mini_player.h"
#include "ui/music_player.h"
#include "ui/online_audio_screen.h"
#include "ui/iptv_screen.h"
#include "ui/network_sources_screen.h"
#include "ui/personal_media_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/settings_screen.h"
#include "ui/text_input.h"
#include "ui/touch.h"

#include <vita_https.h>

static int g_network_ready;
static uint64_t g_startup_started_us;

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
	vt_playback_history_local_id(path, out);
}

static int path_suffix_ci(const char *path, const char *suffix) {
	if (!path || !suffix) return 0;
	size_t path_length = strlen(path), suffix_length = strlen(suffix);
	if (path_length < suffix_length) return 0;
	path += path_length - suffix_length;
	for (size_t i = 0; i < suffix_length; i++) {
		char left = path[i], right = suffix[i];
		if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
		if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
		if (left != right) return 0;
	}
	return 1;
}

static const char *audio_codec_name(const char *path) {
	if (path_suffix_ci(path, ".flac")) return "FLAC";
	if (path_suffix_ci(path, ".mp3")) return "MP3";
	if (path_suffix_ci(path, ".m4a")) return "M4A";
	if (path_suffix_ci(path, ".aac")) return "AAC";
	if (path_suffix_ci(path, ".wav")) return "WAV";
	return "AUDIO";
}

static void remote_media_id(const UiNetworkSelection *selection, char out[16]) {
	vt_network_media_history_id(selection ? &selection->source : NULL,
	                            selection ? selection->path : NULL, out);
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
	if (vt_podcast_companions_move(item->path,target) < 0) {
		sceIoRename(target,item->path);return -1;
	}
	ui_local_files_cache_invalidate();
	if (item->type == VT_LOCAL_MEDIA_IMAGE) return 0;
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
	if (!item || item->is_directory) return -1;
	SceIoStat status;
	if (sceIoGetstat(item->path, &status) < 0 || !SCE_S_ISREG(status.st_mode))
		return -1;
	int ret = sceIoRemove(item->path);
	if (ret < 0) return ret;
	if (vt_podcast_companions_remove(item->path) < 0)
		log_printf("podcast companion cleanup failed\n");
	ui_local_files_cache_invalidate();
	/* A matching basename does not establish ownership of another image or
	 * subtitle. Delete exactly the file named in the confirmation. */
	return 0;
}

static int move_local_media(VtLocalMediaItem *item) {
	if (!item || item->is_directory) return -1;
	char directory[VT_LOCAL_MEDIA_PATH_MAX];
	snprintf(directory, sizeof(directory), "%s", item->path);
	char *slash = strrchr(directory, '/');
	char *colon = strchr(directory, ':');
	if (!colon) return -1;
	if (slash) *slash = '\0';
	else colon[1] = '\0';
	if (!ui_folder_picker(directory, directory, sizeof(directory))) return 1;
	const char *name = strrchr(item->path, '/');
	if (!name) name = strchr(item->path, ':');
	if (!name || !name[1]) return -1;
	char target[VT_LOCAL_MEDIA_PATH_MAX];
	int length = snprintf(target, sizeof(target), "%s%s%s", directory,
	    directory[strlen(directory) - 1] == ':' ? "" : "/", name + 1);
	if (length <= 0 || length >= (int)sizeof(target)) return -1;
	if (!strcmp(item->path, target)) return 1;
	SceIoStat status;
	if (sceIoGetstat(item->path, &status) < 0 || !SCE_S_ISREG(status.st_mode))
		return -1;
	/* Same-mount rename avoids copying gigabytes, partial destination files,
	 * and deleting the source before a copy has been verified. */
	const char *source_mount = strchr(item->path, ':');
	const char *target_mount = strchr(target, ':');
	if (!source_mount || !target_mount ||
	    source_mount - item->path != target_mount - target ||
	    strncmp(item->path, target, (size_t)(source_mount - item->path + 1)))
		return -1;
	if (sceIoGetstat(target, &status) >= 0) return -1;
	int ret = sceIoRename(item->path, target);
	if (ret < 0) return ret;
	if (vt_podcast_companions_move(item->path,target) < 0) {
		sceIoRename(target,item->path);return -1;
	}
	ui_local_files_cache_invalidate();
	return 0;
}

static int local_media_in_use(const VtLocalMediaItem *item) {
	VtBackgroundPlaybackSnapshot snapshot;
	char id[16];
	media_id(item->path, id);
	return vt_background_playback_snapshot(&snapshot) &&
	       !strcmp(snapshot.video_id, id);
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
			vt_video_thumbnail_prepare_playback();
			clear_minimized_remote_video();
			char id[16];
			media_id(item.path, id);
			vt_background_playback_request_stop();
			int ret = vt_background_playback_prepare_local(
			    item.path, id, item.name, item.artist,
			    item.artwork_path[0] ? item.artwork_path : NULL, item.duration_ms);
			VtLibraryItem library_item;
			vt_library_local(&item, &library_item);
			vt_library_cache(&library_item);
			uint64_t resume = vt_library_resume_fallback(
			    &library_item, vt_playback_history_position(id, item.duration_ms));
			VtMusicMetadata podcast_metadata;
			if (ret == 0 && (strstr(item.path, ":podcast/") || strstr(item.path, ":podcasts/") ||
			                 vt_podcast_metadata_load(item.path, &podcast_metadata) == 0))
				resume = vt_background_playback_set_podcast_history(item.path, item.duration_ms);
			if (ret == 0) {
				vt_background_playback_set_library_id(library_item.id);
				ret = vt_background_playback_activate(resume);
				if (!ret)
					vt_library_started(&library_item);
			}
			if (ret < 0) {
				ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
				                vt_i18n_str(VT_STR_MAIN_AUDIO_OPEN_FAILED), 2800);
				memset(&g_minimized_local_audio, 0, sizeof(g_minimized_local_audio));
				return UI_SECTION_LOCAL_MEDIA;
			}
		}
		uint32_t bitrate = item.duration_ms && item.size <= UINT64_MAX / 8ULL
		                       ? (uint32_t)((item.size * 8ULL) / item.duration_ms)
		                       : 0;
		int action = ui_music_player_run(item.artwork_path, item.album, audio_codec_name(item.path),
		                                 bitrate);
		if (action >= UI_MUSIC_PLAYER_SECTION_BASE) {
			remember_minimized_local_audio(&item);
			if (navigated)
				*navigated = 1;
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
	                                  int *last_subtitle_track, int *paused) {
	if (!item) return -1;
	vt_video_thumbnail_prepare_playback();
	VtLibraryItem library_item;
	vt_library_local(item, &library_item);
	vt_library_bind(&library_item);
	char id[16];
	media_id(item->path, id);
	VtDecoderStreamFactory factory;
	vt_decoder_file_stream_factory(item->path, &factory);
	VtHwPlayerScreenSource source = {.stream = factory,
	                                 .title = item->name,
	                                 .location = vt_i18n_str(VT_STR_LOCAL_MEDIA_TITLE),
	                                 .history_id = id,
	                                 .authenticated_remote = 0,
	                                 .allow_minimize = 1,
	                                 .start_position_ms = start_position_ms,
	                                 .start_audio_track = start_audio_track,
	                                 .start_paused = paused ? *paused : 0,
	                                 .start_subtitle_track = start_subtitle_track,
	                                 .expected_width = 0,
	                                 .expected_height = 0,
	                                 .expected_fps = 0};
	return vt_hw_player_screen_run(&source, last_position_ms, last_duration_ms, last_audio_track,
	                               last_subtitle_track, paused);
}

static int minimize_local_video(const VtLocalMediaItem *item,
	                            uint64_t position_ms,
	                            int audio_track,
	                            int subtitle_track, int paused) {
	if (!item) return -1;
	char id[16];
	media_id(item->path, id);
	int ret = vt_background_playback_prepare_local_video(
	    item->path, id, item->name,
	    item->artist[0] ? item->artist : vt_i18n_str(VT_STR_LOCAL_MEDIA_TITLE),
	    item->artwork_path[0] ? item->artwork_path : NULL,
	    item->duration_ms, audio_track);
	if (ret < 0) return ret;
	VtLibraryItem library_item;
	vt_library_local(item, &library_item);
	vt_background_playback_set_library_id(library_item.id);
	vt_background_playback_configure_video(subtitle_track, NULL);
	g_minimized_local_video.valid = 1;
	g_minimized_local_video.item = *item;
	g_minimized_local_video.audio_track = audio_track;
	g_minimized_local_video.subtitle_track = subtitle_track;
	vt_background_playback_set_fullscreen_resume(resume_minimized_local_video,
	                                             &g_minimized_local_video);
	ret = vt_background_playback_activate_state(position_ms, paused);
	log_printf("player handoff: local mini position=%llu paused=%d audio=%d subtitle=%d ret=%d\n",
	           (unsigned long long)position_ms, paused, audio_track, subtitle_track, ret);
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
	int paused = vt_background_playback_is_paused();
	log_printf("player handoff: local full position=%llu paused=%d audio=%d subtitle=%d\n",
	           (unsigned long long)position_ms, paused, audio_track, subtitle_track);
	vt_background_playback_stop();
	uint64_t last_position = position_ms;
	uint64_t last_duration = item.duration_ms;
	int last_audio_track = audio_track;
	int last_subtitle_track = subtitle_track;
	int ret = run_local_video_fullscreen(
	    &item, position_ms, audio_track, subtitle_track, &last_position,
	    &last_duration, &last_audio_track, &last_subtitle_track, &paused);
	char id[16];
	media_id(item.path, id);
	vt_playback_history_update(id, last_position,
	                           last_duration ? last_duration : item.duration_ms);
	if (ret == VT_HW_PLAYER_ACTION_MINIMIZE) {
		if (minimize_local_video(&item, last_position, last_audio_track,
		                         last_subtitle_track, paused) < 0) {
			ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
			                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
			return -1;
		}
		return 0;
	}
	if (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
	    ret < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT) {
		if (minimize_local_video(&item, last_position, last_audio_track,
		                         last_subtitle_track, paused) < 0) {
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
	vt_video_thumbnail_suspend();
	clear_minimized_remote_video();
	char id[16];
	media_id(item->path, id);
	vt_background_playback_set_fullscreen_resume(NULL, NULL);
	vt_background_playback_stop();
	VtLibraryItem library_item;
	vt_library_local(item, &library_item);
	vt_library_cache(&library_item);
	uint64_t last_position = vt_library_resume_fallback(
	    &library_item, vt_playback_history_position(id, item->duration_ms));
	uint64_t last_duration = item->duration_ms;
	int last_audio_track = 0;
	int last_subtitle_track = 0;
	int paused = 0;
	int ret = run_local_video_fullscreen(item, last_position, 0, 0, &last_position, &last_duration,
	                                     &last_audio_track, &last_subtitle_track, &paused);
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	vt_playback_history_update(id, last_position,
	                           last_duration ? last_duration : item->duration_ms);
	if (ret == VT_HW_PLAYER_ACTION_MINIMIZE) {
		if (minimize_local_video(item, last_position, last_audio_track,
		                         last_subtitle_track, paused) < 0)
			ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
			                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
		return UI_SECTION_LOCAL_MEDIA;
	}
	if (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
	    ret < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT) {
		if (minimize_local_video(item, last_position, last_audio_track,
		                         last_subtitle_track, paused) < 0)
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
	int *last_subtitle_track, int *paused) {
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
	VtNetworkEntry entry = {.is_video = 1};
	snprintf(entry.path, sizeof(entry.path), "%s", selection->path);
	snprintf(entry.name, sizeof(entry.name), "%s", selection->title);
	VtLibraryItem library_item;
	vt_library_remote(&selection->source, &entry, NULL, &library_item);
	vt_library_bind(&library_item);
	VtHwPlayerScreenSource source = {.stream = remote.factory,
	                                 .title = selection->title,
	                                 .location = selection->source.name,
	                                 .history_id = id,
	                                 .authenticated_remote = 1,
	                                 .allow_minimize = 1,
	                                 .start_position_ms = start_position_ms,
	                                 .start_audio_track = start_audio_track,
	                                 .start_paused = paused ? *paused : 0,
	                                 .start_subtitle_track = start_subtitle_track};
	if (selection->has_jellyfin_metadata && selection->jellyfin_metadata.media_source_id[0]) {
		int count = selection->jellyfin_metadata.external_subtitle_count;
		if (count > VT_JELLYFIN_MAX_EXTERNAL_SUBTITLES)
			count = VT_JELLYFIN_MAX_EXTERNAL_SUBTITLES;
		for (int i = 0; i < count; i++) {
			const VtJellyfinSubtitleTrack *track =
			    &selection->jellyfin_metadata.external_subtitles[i];
			if (vt_network_jellyfin_subtitle_stream_factory_init(
			        &subtitle_factories[source.external_subtitle_count], &selection->source,
			        &selection->credential, selection->path,
			        selection->jellyfin_metadata.media_source_id, track->stream_index) < 0)
				continue;
			VtDecoderExternalSubtitle *external =
			    &source.external_subtitles[source.external_subtitle_count++];
			external->stream = subtitle_factories[source.external_subtitle_count - 1].factory;
			external->info.stream_index = 0;
			external->info.is_default = track->is_default;
			snprintf(external->info.language, sizeof(external->info.language), "%s",
			         track->language);
			snprintf(external->info.title, sizeof(external->info.title), "%s", track->title);
			snprintf(external->info.codec, sizeof(external->info.codec), "%s",
			         track->codec[0] ? track->codec : "subrip");
		}
		if (source.external_subtitle_count > 0)
			source.external_subtitles_replace_embedded = 1;
	}
	ret = vt_hw_player_screen_run(&source, last_position_ms, last_duration_ms, last_audio_track,
	                              last_subtitle_track, paused);
	memset(&remote.credential, 0, sizeof(remote.credential));
	for (int i = 0; i < source.external_subtitle_count; i++)
		memset(&subtitle_factories[i].credential, 0, sizeof(subtitle_factories[i].credential));
	return ret;
}

static int minimize_remote_video(const UiNetworkSelection *selection,
	                             uint64_t position_ms,
	                             uint64_t duration_ms,
	                             int audio_track,
	                             int subtitle_track, int paused) {
	if (!selection) return -1;
	char id[16];
	remote_media_id(selection, id);
	int ret = vt_background_playback_prepare_remote_video(
	    &selection->source, &selection->credential, selection->path, id,
	    selection->title, selection->source.name, duration_ms, audio_track);
	if (ret < 0) return ret;
	VtNetworkEntry entry = {.is_video = 1};
	snprintf(entry.path, sizeof(entry.path), "%s", selection->path);
	snprintf(entry.name, sizeof(entry.name), "%s", selection->title);
	VtLibraryItem library_item;
	vt_library_remote(&selection->source, &entry, NULL, &library_item);
	vt_background_playback_set_library_id(library_item.id);
	vt_background_playback_configure_video(
	    subtitle_track, selection->has_jellyfin_metadata ? &selection->jellyfin_metadata : NULL);
	g_minimized_remote_video.valid = 1;
	g_minimized_remote_video.selection = *selection;
	g_minimized_remote_video.audio_track = audio_track;
	g_minimized_remote_video.subtitle_track = subtitle_track;
	vt_background_playback_set_fullscreen_resume(resume_minimized_remote_video,
	                                             &g_minimized_remote_video);
	ret = vt_background_playback_activate_state(position_ms, paused);
	log_printf("player handoff: remote mini position=%llu paused=%d audio=%d subtitle=%d ret=%d\n",
	           (unsigned long long)position_ms, paused, audio_track, subtitle_track, ret);
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
	int paused = vt_background_playback_is_paused();
	log_printf("player handoff: remote full position=%llu paused=%d audio=%d subtitle=%d\n",
	           (unsigned long long)position_ms, paused, audio_track, subtitle_track);
	vt_background_playback_stop();
	uint64_t last_position = position_ms;
	uint64_t last_duration = 0;
	int last_audio_track = audio_track;
	int last_subtitle_track = subtitle_track;
	int ret = run_remote_video_fullscreen(
	    &selection, position_ms, audio_track, subtitle_track,
	    &last_position, &last_duration, &last_audio_track,
	    &last_subtitle_track, &paused);
	char id[16];
	remote_media_id(&selection, id);
	vt_playback_history_update(id, last_position, last_duration);
	if (ret == VT_HW_PLAYER_ACTION_MINIMIZE ||
	    (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
	     ret < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT)) {
		if (minimize_remote_video(&selection, last_position, last_duration,
		                          last_audio_track, last_subtitle_track, paused) < 0) {
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

static int play_remote_selection(UiNetworkSelection selection) {
	VtNetworkEntry entry = {.is_video = 1};
	snprintf(entry.path, sizeof(entry.path), "%s", selection.path);
	snprintf(entry.name, sizeof(entry.name), "%s", selection.title);
	VtLibraryItem library_item;
	vt_library_remote(&selection.source, &entry, NULL, &library_item);
	vt_library_cache(&library_item);
	char id[16];
	remote_media_id(&selection, id);
	if (active_media_id_matches(id)) {
		/* Selecting the same remote cell is a restore action, exactly like the
		 * local grid. Preserve the live mini-player position and chosen tracks. */
		memset(&selection.credential, 0, sizeof(selection.credential));
		return UI_SECTION_PLAYER;
	}
	uint64_t last_position =
	    vt_library_resume_fallback(&library_item, vt_playback_history_position(id, 0));
	uint64_t last_duration = 0;
	vt_background_playback_stop();
	clear_minimized_remote_video();
	int last_audio_track = 0;
	int last_subtitle_track = 0;
	int paused = 0;
	int ret =
	    run_remote_video_fullscreen(&selection, last_position, 0, 0, &last_position, &last_duration,
		                            &last_audio_track, &last_subtitle_track, &paused);
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	vt_playback_history_update(id, last_position, last_duration);
	if (ret == VT_HW_PLAYER_ACTION_MINIMIZE ||
	    (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
	     ret < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT)) {
		if (minimize_remote_video(&selection, last_position, last_duration, last_audio_track,
		                          last_subtitle_track, paused) < 0)
			ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
			                vt_i18n_str(VT_STR_MAIN_VIDEO_MINI_FAILED), 3000);
		memset(&selection.credential, 0, sizeof(selection.credential));
		if (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE)
			return ret - VT_HW_PLAYER_ACTION_SECTION_BASE;
		return UI_SECTION_NONE;
	}
	memset(&selection.credential, 0, sizeof(selection.credential));
	if (ret < 0)
		ui_message_show(vt_i18n_str(VT_STR_MAIN_STREAMING_FAILED),
		                vt_i18n_str(VT_STR_MAIN_STREAMING_DETAIL), 3200);
	return UI_SECTION_NONE;
}

typedef struct {
	UiNetworkSelection *selection;
} LibraryMetadataTask;
static int library_metadata(void *ctx) {
	UiNetworkSelection *s = ((LibraryMetadataTask *)ctx)->selection;
	char detail[192];
	int ret = vt_network_jellyfin_metadata(&s->source, &s->credential, s->path,
	                                       &s->jellyfin_metadata, detail, sizeof(detail), NULL);
	s->has_jellyfin_metadata = ret >= 0;
	return ret;
}
static int play_library_remote(const VtLibraryItem *v) {
	UiNetworkSelection s = {0};
	if (vt_library_source(v, &s.source, &s.credential) < 0) {
		ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
		                vt_i18n_current_language() == VT_LANG_IT
		                    ? "La sorgente e stata rimossa. Controlla Sorgenti."
							: "Source removed. Check Sources.",
		                2800);
		return UI_SECTION_LOCAL_MEDIA;
	}
	if (ui_network_library_authorize(&s.source, &s.credential) < 0) {
		memset(&s.credential, 0, sizeof(s.credential));
		return UI_SECTION_LOCAL_MEDIA;
	}
	snprintf(s.path, sizeof(s.path), "%s", v->locator);
	snprintf(s.title, sizeof(s.title), "%s", v->title);
	int result;
	if (v->provider == VT_LIBRARY_NAVIDROME || v->provider == VT_LIBRARY_IMMICH) {
		VtNetworkEntry entry = {.is_audio = v->kind == VT_LIBRARY_AUDIO,
		                        .is_image = v->kind == VT_LIBRARY_IMAGE,
		                        .runtime_ms = v->duration_ms};
		snprintf(entry.path, sizeof(entry.path), "%s", v->locator);
		snprintf(entry.name, sizeof(entry.name), "%s", v->title);
		snprintf(entry.artist, sizeof(entry.artist), "%s", v->artist);
		snprintf(entry.album, sizeof(entry.album), "%s", v->album);
		result = ui_personal_media_open(&s.source, &s.credential, &entry, &entry, 1, 0);
		result = result >= UI_NETWORK_ACTION_SECTION_BASE ? result - UI_NETWORK_ACTION_SECTION_BASE
		                                                  : UI_SECTION_LOCAL_MEDIA;
	} else {
		if (v->provider == VT_LIBRARY_JELLYFIN) {
			LibraryMetadataTask task = {&s};
			ui_loading_run("Jellyfin", library_metadata, &task, NULL, NULL, NULL);
		}
		result = play_remote_selection(s);
		if (result == UI_SECTION_NONE)
			result = UI_SECTION_LOCAL_MEDIA;
	}
	memset(&s.credential, 0, sizeof(s.credential));
	return result;
}
static int browse_local(void) {
	if(ui_home_tab_online()){ui_online_audio_set_home(1);return UI_SECTION_ONLINE_AUDIO;}
	int folder_browser = 0;
	int folder_resume = 0;
	char folder_root[VT_LOCAL_MEDIA_PATH_MAX] = "";
	VtLocalMediaType folder_filter = 0;
	for (;;) {
		VtLocalMediaItem item = {0};
		int library_action = -1;
		if (ui_home_tab() == HOME_LIBRARY && !folder_browser) {
			VtLibraryItem chosen;
			int action = ui_library_screen(&chosen);
			if (action >= UI_LIBRARY_SECTION_BASE) {
				int section = action - UI_LIBRARY_SECTION_BASE;
				if (section == UI_SECTION_ONLINE_AUDIO)
					ui_online_audio_set_home(0);
				return section;
			}
			if (action == UI_LIBRARY_LOCAL) {
                /* The unified Home opens the filesystem directly; never enter
                 * the old indexed Home while its Home tab remains selected. */
                folder_browser = 1;
                folder_resume = 0;
                folder_root[0] = '\0';
                folder_filter = 0;
				continue;
			}
			if (action == UI_LIBRARY_TAB) {
				if (ui_home_tab_online()) {
					ui_online_audio_set_home(1);
					return UI_SECTION_ONLINE_AUDIO;
				}
				continue;
			}
			if (action == UI_LIBRARY_CONNECT) {
				if (!g_network_ready) {
					ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
					                vt_i18n_str(VT_STR_MAIN_LOCAL_AVAILABLE), 2600);
					continue;
				}
				VtNetworkSource source;
				VtNetworkCredential credential = {0};
				int resolved = vt_library_source(&chosen, &source, &credential);
				if (resolved >= 0)
					for (;;) {
						UiNetworkSelection selection = {0};
						int opened = ui_network_browse_saved(&source, &credential, &selection);
						if (opened >= UI_NETWORK_ACTION_SECTION_BASE) {
							memset(&credential, 0, sizeof(credential));
							return opened - UI_NETWORK_ACTION_SECTION_BASE;
						}
						if (opened != UI_NETWORK_ACTION_PLAY)
							break;
						int next = play_remote_selection(selection);
						memset(&selection.credential, 0, sizeof(selection.credential));
						if (next != UI_SECTION_NONE) {
							memset(&credential, 0, sizeof(credential));
							return next;
						}
					}
				memset(&credential, 0, sizeof(credential));
				continue;
			}
			if (action != UI_LIBRARY_PLAY)
				continue;
			if (vt_background_playback_library_matches(chosen.id))
				return UI_SECTION_PLAYER;
			if (chosen.provider != VT_LIBRARY_LOCAL && !g_network_ready) {
				ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
				                vt_i18n_str(VT_STR_MAIN_LOCAL_AVAILABLE), 2600);
				continue;
			}
			if (chosen.provider == VT_LIBRARY_LOCAL) {
				snprintf(item.path, sizeof(item.path), "%s", chosen.locator);
				snprintf(item.name, sizeof(item.name), "%s", chosen.title);
				snprintf(item.artist, sizeof(item.artist), "%s", chosen.artist);
				snprintf(item.album, sizeof(item.album), "%s", chosen.album);
				snprintf(item.artwork_path, sizeof(item.artwork_path), "%s", chosen.artwork);
				item.duration_ms = chosen.duration_ms;
				item.size = chosen.size;
				item.type = (VtLocalMediaType)chosen.kind;
				if (chosen.kind == VT_LIBRARY_FOLDER) {
					folder_browser = 1;
					folder_resume = 0;
					snprintf(folder_root, sizeof(folder_root), "%s", chosen.locator);
					folder_filter = 0;
					continue;
				}
				SceIoStat st;
				if (sceIoGetstat(item.path, &st) < 0) {
					ui_message_show(vt_i18n_str(VT_STR_MAIN_PLAYBACK_FAILED),
					                vt_i18n_current_language() == VT_LANG_IT
					                    ? "File non disponibile: controlla la scheda di memoria."
										: "File unavailable: check the memory card.",
					                2800);
					continue;
				}
				library_action = UI_LOCAL_MEDIA_ACTION_PLAY;
			} else if (chosen.provider == VT_LIBRARY_RADIO ||
			           chosen.provider == VT_LIBRARY_PODCAST) {
				int ret = ui_online_audio_open_library(&chosen);
				if (ret >= UI_MUSIC_PLAYER_SECTION_BASE)
					return ret - UI_MUSIC_PLAYER_SECTION_BASE;
				continue;
			} else if (chosen.provider == VT_LIBRARY_IPTV) {
				int next = ui_iptv_open_library(&chosen);
				if (next != UI_SECTION_NONE)
					return next;
				continue;
			} else {
				int next = play_library_remote(&chosen);
				if (next != UI_SECTION_LOCAL_MEDIA)
					return next;
				continue;
			}
		}
		int action = library_action >= 0 ? library_action
		             : folder_browser
		                 ? ui_local_files_screen_open(folder_resume ? NULL : folder_root,
		                                              folder_filter, &item)
		                 : ui_local_media_screen(&item);
		if (folder_browser)
			folder_resume = 1;
		if (action >= UI_LOCAL_MEDIA_ACTION_SECTION_BASE)
			return action - UI_LOCAL_MEDIA_ACTION_SECTION_BASE;
		if (action == UI_LOCAL_MEDIA_ACTION_BACK) {
			if (folder_browser) {
				folder_browser = folder_resume = 0;
				folder_root[0] = '\0';
				continue;
			}
			if (ui_home_tab() == HOME_PODCAST)
				ui_home_podcast_set_offline(0);
			return UI_SECTION_LOCAL_MEDIA;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_BROWSE_FILES) {
			folder_browser = 1;
			folder_resume = 0;
			folder_root[0] = '\0';
			folder_filter = 0;
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_BROWSE_FOLDER) {
			folder_browser = 1;
			folder_resume = 0;
			snprintf(folder_root, sizeof(folder_root), "%s", item.path);
			folder_filter = item.type;
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_MANAGE) {
			action = ui_local_media_actions(&item);
			if (!action)
				continue;
		}
		if ((action == UI_LOCAL_MEDIA_ACTION_RENAME || action == UI_LOCAL_MEDIA_ACTION_DELETE ||
		     action == UI_LOCAL_MEDIA_ACTION_MOVE) &&
		    local_media_in_use(&item)) {
			ui_message_show(vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_FAILED),
			                vt_i18n_str(VT_STR_LOCAL_MEDIA_IN_USE), 2600);
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_RENAME) {
			int result = rename_local_media(&item);
			if (result == 0)
				ui_local_media_cache_invalidate();
			if (result < 0)
				ui_message_show(vt_i18n_str(VT_STR_MAIN_RENAME_FAILED),
				                vt_i18n_str(VT_STR_MAIN_RENAME_UNCHANGED), 2400);
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_MOVE) {
			int result = move_local_media(&item);
			if (result == 0)
				ui_local_media_cache_invalidate();
			if (result < 0)
				ui_message_show(vt_i18n_str(VT_STR_LOCAL_MEDIA_MOVE_FAILED),
				                vt_i18n_str(VT_STR_LOCAL_MEDIA_MOVE_DETAIL), 3000);
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_DELETE) {
			if (!ui_local_media_confirm_delete(&item))
				continue;
			int result = delete_local_media(&item);
			if (result == 0)
				ui_local_media_cache_invalidate();
			if (result < 0)
				ui_message_show(vt_i18n_str(VT_STR_MAIN_DELETE_FAILED),
				                vt_i18n_str(VT_STR_MAIN_DELETE_UNCHANGED), 2400);
			continue;
		}
		if (action == UI_LOCAL_MEDIA_ACTION_PLAY) {
			if (item.type == VT_LOCAL_MEDIA_AUDIO) {
				int section = play_local_audio(item);
				if (section != UI_SECTION_LOCAL_MEDIA)
					return section;
			} else if (item.type == VT_LOCAL_MEDIA_IMAGE) {
				VtLibraryItem library_item;
				vt_library_local(&item, &library_item);
				vt_library_started(&library_item);
				int image_action = ui_image_viewer_show(item.path, item.name);
				if (image_action >= UI_IMAGE_VIEWER_SECTION_BASE)
					return image_action - UI_IMAGE_VIEWER_SECTION_BASE;
			} else {
				int section = play_local_video(&item);
				if (section != UI_SECTION_LOCAL_MEDIA)
					return section;
			}
		}
	}
}

static int browse_network(int provider_section) {
	if (!g_network_ready) {
		ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
		                vt_i18n_str(VT_STR_MAIN_LOCAL_AVAILABLE), 2600);
		return UI_SECTION_NETWORK;
	}
	for (;;) {
		UiNetworkSelection selection;
		int action = provider_section==UI_SECTION_JELLYFIN?ui_jellyfin_sources_screen(&selection):ui_network_sources_screen(&selection);
		if (action >= UI_NETWORK_ACTION_SECTION_BASE)
			return action - UI_NETWORK_ACTION_SECTION_BASE;
		if (action != UI_NETWORK_ACTION_PLAY) return provider_section==UI_SECTION_JELLYFIN?UI_SECTION_LOCAL_MEDIA:UI_SECTION_NETWORK;
		int next = play_remote_selection(selection);
		memset(&selection.credential, 0, sizeof(selection.credential));
		if (next != UI_SECTION_NONE)
			return next;
	}
}

static int run_application(void) {
	uint64_t stage_started_us = sceKernelGetProcessTimeWide();
	log_printf("startup: playback/history begin");
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	vt_playback_history_init();
	/* Initialize the sampled state before either checkpoint worker can read it. */
	vt_background_playback_init();
	int library_ret = vt_library_init(vt_background_playback_library_sample);
	if (library_ret < 0)
		log_printf("library checkpoint worker unavailable: %d", library_ret);
	log_printf(
	    "startup stage: playback/history=%llu ms total=%llu ms",
	    (unsigned long long)((sceKernelGetProcessTimeWide() - stage_started_us) / 1000ULL),
	    (unsigned long long)((sceKernelGetProcessTimeWide() - g_startup_started_us) / 1000ULL));
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	ui_loading_present(NULL);
	stage_started_us = sceKernelGetProcessTimeWide();
	log_printf("startup: network begin");
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	g_network_ready = vita_https_init() >= 0;
	if (g_network_ready)
		g_network_ready = vt_network_init() >= 0;
	log_printf(
	    "startup stage: network=%llu ms ready=%d total=%llu ms",
	    (unsigned long long)((sceKernelGetProcessTimeWide() - stage_started_us) / 1000ULL),
	    g_network_ready,
	    (unsigned long long)((sceKernelGetProcessTimeWide() - g_startup_started_us) / 1000ULL));
	log_printf("startup: search workers begin");
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	int search_ret = vt_library_search_init();
	log_printf("startup: search workers complete result=%d; opening Home", search_ret);
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	int section = UI_SECTION_LOCAL_MEDIA;
	if (!vt_preferences_startup_controls_seen()) {
		int controls_section = ui_settings_show_controls_reference();
		if (controls_section != UI_SECTION_NONE)
			section = controls_section;
		vt_preferences_set_startup_controls_seen(1);
		ui_touch_reset();
	}
	int return_section = UI_SECTION_LOCAL_MEDIA;
	for (;;) {
		if (section == UI_SECTION_DOWNLOAD) {
			int download_action = ui_loading_monitor_background_download();
			section = download_action >= UI_LOADING_DOWNLOAD_SECTION_BASE
			              ? download_action - UI_LOADING_DOWNLOAD_SECTION_BASE
			              : return_section;
			continue;
		}
		if (section == UI_SECTION_PLAYER) {
			int action = vt_background_playback_resume_fullscreen();
			ui_sections_take_requested();
			section = action >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
			                  action < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT
			              ? action - VT_HW_PLAYER_ACTION_SECTION_BASE
			              : return_section;
			continue;
		}
		return_section = section;
		if (section == UI_SECTION_LOCAL_MEDIA)
			section = browse_local();
		else if (section == UI_SECTION_NETWORK || section == UI_SECTION_JELLYFIN) {
			if (!g_network_ready) {
				ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
				                vt_i18n_str(VT_STR_MAIN_LOCAL_AVAILABLE), 2600);
				section = UI_SECTION_LOCAL_MEDIA;
			} else
				section = browse_network(section);
		} else if (section == UI_SECTION_NAVIDROME || section == UI_SECTION_IMMICH) {
			section = ui_personal_media_screen(
			    section == UI_SECTION_NAVIDROME ? VT_NETWORK_NAVIDROME : VT_NETWORK_IMMICH);
		} else if (section == UI_SECTION_SETTINGS) {
			int next = ui_settings_screen();
			section = next == UI_SECTION_NONE ? UI_SECTION_LOCAL_MEDIA : next;
		} else if (section == UI_SECTION_INFO) {
			int next = ui_about_screen(0);
			section = next == UI_SECTION_NONE ? UI_SECTION_LOCAL_MEDIA : next;
		} else if (section == UI_SECTION_ONLINE_AUDIO) {
			section = ui_online_audio_screen();
		} else if (section == UI_SECTION_IPTV) {
			if (g_network_ready)
				section = ui_iptv_screen();
			else {
				ui_message_show(vt_i18n_str(VT_STR_MAIN_NETWORK_UNAVAILABLE),
				                vt_i18n_str(VT_STR_MAIN_LOCAL_AVAILABLE), 2600);
				section = UI_SECTION_LOCAL_MEDIA;
			}
		} else if (section == UI_SECTION_GUIDE) {
			int next = ui_settings_show_controls_reference();
			section = next == UI_SECTION_NONE ? UI_SECTION_LOCAL_MEDIA : next;
		} else
			section = UI_SECTION_LOCAL_MEDIA;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	log_init();
    log_printf("build: id=%s bubble=%s version=%s sw_upload=%s log=%s",
               VT_BUILD_TEST_ID, VT_BUILD_NAME, VT_BUILD_VERSION,
               VT_BUILD_SW_UPLOAD, VITAMEDIADECK_SESSION_LOG_PATH);
	g_startup_started_us = sceKernelGetProcessTimeWide();
	uint64_t stage_started_us = g_startup_started_us;
    int preferences_result = vt_preferences_init();
    if (preferences_result < 0) {
        log_printf("preferences: load/migration failed 0x%08X", (unsigned)preferences_result);
    }
	SceCtrlData startup_controls = {0};
	sceCtrlPeekBufferPositive(0, &startup_controls, 1);
	if ((startup_controls.buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) ==
	        (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER) &&
	    vt_preferences_interface_theme() == VT_UI_THEME_LEGACY_VITA) {
		vt_preferences_set_interface_theme(VT_UI_THEME_LEGACY);
		log_printf("native PAF: startup L1+R1 recovery selected Legacy");
	}
	vt_i18n_init();
	log_printf("startup stage: preferences/i18n=%llu ms",
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                stage_started_us) / 1000ULL));
	log_printf("startup: diagnostics schema=2 theme=%d; graphics begin",
	           vt_preferences_interface_theme());
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	stage_started_us = sceKernelGetProcessTimeWide();
	if (ui_runtime_init() < 0) {
		sceKernelExitProcess(0);
		return 0;
	}
	log_printf("startup stage: graphics=%llu ms",
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                stage_started_us) / 1000ULL));
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	uint64_t assets_started_us = sceKernelGetProcessTimeWide();
	ui_runtime_load_boot_assets();
	ui_loading_present(NULL);
	log_printf("startup assets: boot-visible=%llu ms total=%llu ms",
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                assets_started_us) / 1000ULL),
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                g_startup_started_us) / 1000ULL));
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	ui_runtime_load_assets();
	log_printf("startup assets: complete=%llu ms total=%llu ms",
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                assets_started_us) / 1000ULL),
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                g_startup_started_us) / 1000ULL));
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	ui_touch_init();
	stage_started_us = sceKernelGetProcessTimeWide();
	int thumbnail_ret = vt_video_thumbnail_init();
	if (thumbnail_ret < 0)
		log_printf("video thumbnail worker unavailable: 0x%08X",
		           (unsigned)thumbnail_ret);
	log_printf("startup stage: thumbnail-worker=%llu ms total=%llu ms",
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                stage_started_us) / 1000ULL),
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                g_startup_started_us) / 1000ULL));
	run_application();
	vt_library_search_shutdown();
	ui_iptv_shutdown();
	vt_video_thumbnail_shutdown();
	vt_background_playback_shutdown();
	vt_library_shutdown();
	ui_personal_media_shutdown();
	ui_mini_player_shutdown();
	vt_download_background_shutdown();
	vt_network_shutdown();
	vita_https_shutdown();
	ui_touch_term();
	ui_runtime_term();
	sceKernelExitProcess(0);
	return 0;
}
