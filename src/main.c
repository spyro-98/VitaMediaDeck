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

static void media_id(const char *path, char out[16]) {
	uint32_t hash = 2166136261U;
	for (const unsigned char *cursor = (const unsigned char *)path;
	     cursor && *cursor; cursor++) {
		hash ^= *cursor;
		hash *= 16777619U;
	}
	snprintf(out, 16, "media%08x", hash);
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

static int play_local_audio(VtLocalMediaItem item) {
	for (;;) {
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
			return 0;
		}
		uint32_t bitrate = item.duration_ms && item.size <= UINT64_MAX / 8ULL
		                   ? (uint32_t)((item.size * 8ULL) / item.duration_ms) : 0;
		int action = ui_music_player_run(item.artwork_path, item.album, bitrate);
		if (action >= UI_MUSIC_PLAYER_SECTION_BASE)
			return action - UI_MUSIC_PLAYER_SECTION_BASE;
		if (action == UI_MUSIC_PLAYER_STOP || action == UI_MUSIC_PLAYER_MINIMIZE)
			return UI_SECTION_LOCAL_MEDIA;
		VtLocalMediaItem next;
		if (action != UI_MUSIC_PLAYER_REPEAT &&
		    ui_local_media_next_audio(item.path, 1,
		                              ui_music_player_shuffle_enabled(), &next) < 0)
			return UI_SECTION_LOCAL_MEDIA;
		if (action != UI_MUSIC_PLAYER_REPEAT) item = next;
	}
}

static int play_local_video(const VtLocalMediaItem *item) {
	char id[16];
	media_id(item->path, id);
	VtDecoderStreamFactory factory;
	vt_decoder_file_stream_factory(item->path, &factory);
	VtHwPlayerScreenSource source = {
		.stream = factory,
		.title = item->name,
		.location = vt_i18n_str(VT_STR_LOCAL_MEDIA_TITLE),
		.authenticated_remote = 0,
		.start_position_ms = vt_playback_history_position(id, item->duration_ms),
		.expected_width = 0,
		.expected_height = 0,
		.expected_fps = 0
	};
	uint64_t last_position = 0;
	int ret = vt_hw_player_screen_run(&source, &last_position);
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	vt_playback_history_update(id, last_position, item->duration_ms);
	if (ret < 0)
		ui_message_show(vt_i18n_str(VT_STR_MAIN_UNSUPPORTED_MEDIA),
		                vt_i18n_str(VT_STR_MAIN_UNSUPPORTED_DETAIL), 3200);
	if (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE)
		return ret - VT_HW_PLAYER_ACTION_SECTION_BASE;
	return UI_SECTION_LOCAL_MEDIA;
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
		VtNetworkStreamFactory remote;
		if (vt_network_stream_factory_init(&remote, &selection.source,
		                                  &selection.credential,
		                                  selection.path) < 0) continue;
		VtHwPlayerScreenSource source = {
			.stream = remote.factory,
			.title = selection.title,
			.location = selection.source.name,
			.authenticated_remote = 1
		};
		vt_background_playback_request_stop();
		uint64_t ignored = 0;
		int ret = vt_hw_player_screen_run(&source, &ignored);
		log_save(VITAMEDIADECK_SESSION_LOG_PATH);
		memset(&selection.credential, 0, sizeof(selection.credential));
		memset(&remote.credential, 0, sizeof(remote.credential));
		if (ret < 0)
			ui_message_show(vt_i18n_str(VT_STR_MAIN_STREAMING_FAILED),
			                vt_i18n_str(VT_STR_MAIN_STREAMING_DETAIL), 3200);
		if (ret >= VT_HW_PLAYER_ACTION_SECTION_BASE)
			return ret - VT_HW_PLAYER_ACTION_SECTION_BASE;
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
	for (;;) {
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
