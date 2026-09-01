#include "ui/local_files_screen.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "common/text_log.h"
#include "i18n/i18n.h"
#include "media/image_loader.h"
#include "media/music_metadata.h"
#include "media/video_thumbnail.h"
#include "history/playback_history.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/focus_glow.h"
#include "ui/format.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define FILE_MAX_ENTRIES 256
#define FILE_X 66
#define FILE_Y 112
#define FILE_W 828
#define FILE_ROW_H 58
#define FILE_GRID_COLS 4
#define FILE_CARD_W 198
#define FILE_CARD_H 154
#define FILE_GAP_X 12
#define FILE_GAP_Y 10
#define FILE_DIRECTORY_CACHE_SLOTS 4

typedef struct {
	VtLocalMediaItem media;
	int is_directory;
} LocalFileEntry;

static char g_last_path[VT_LOCAL_MEDIA_PATH_MAX];
static char g_browser_root[VT_LOCAL_MEDIA_PATH_MAX];
static VtLocalMediaType g_type_filter;

typedef struct {
	int valid;
	char path[VT_LOCAL_MEDIA_PATH_MAX];
	VtLocalMediaType filter;
	uint64_t signature;
	uint64_t last_used;
	int count;
	int selected;
	int top;
	LocalFileEntry *entries;
} LocalDirectoryCache;

static LocalDirectoryCache g_directory_cache[FILE_DIRECTORY_CACHE_SLOTS];
static uint64_t g_directory_cache_clock;

static uint64_t directory_signature(const char *path) {
	if (!path || !path[0]) return 0;
	SceIoStat status;
	memset(&status, 0, sizeof(status));
	if (sceIoGetstat(path, &status) < 0) return 0;
	uint64_t hash = 1469598103934665603ULL;
	const unsigned char *fields = (const unsigned char *)&status.st_mtime;
	for (size_t i = 0; i < sizeof(status.st_mtime); i++) {
		hash ^= fields[i];
		hash *= 1099511628211ULL;
	}
	fields = (const unsigned char *)&status.st_ctime;
	for (size_t i = 0; i < sizeof(status.st_ctime); i++) {
		hash ^= fields[i];
		hash *= 1099511628211ULL;
	}
	return hash ? hash : 1;
}

void ui_local_files_cache_invalidate(void) {
	for (int i = 0; i < FILE_DIRECTORY_CACHE_SLOTS; i++) {
		free(g_directory_cache[i].entries);
		memset(&g_directory_cache[i], 0, sizeof(g_directory_cache[i]));
	}
}

static LocalDirectoryCache *directory_cache_find(const char *path,
	                                              uint64_t signature) {
	if (!path || !path[0] || !signature) return NULL;
	for (int i = 0; i < FILE_DIRECTORY_CACHE_SLOTS; i++) {
		LocalDirectoryCache *slot = &g_directory_cache[i];
		if (slot->valid && slot->filter == g_type_filter &&
		    slot->signature == signature && !strcmp(slot->path, path)) {
			slot->last_used = ++g_directory_cache_clock;
			return slot;
		}
	}
	return NULL;
}

static void directory_cache_store(const char *path,
	                              const LocalFileEntry *entries, int count,
	                              int selected, int top) {
	uint64_t signature = directory_signature(path);
	if (!path || !path[0] || !signature || count < 0 ||
	    count > FILE_MAX_ENTRIES) return;
	LocalDirectoryCache *slot = NULL;
	for (int i = 0; i < FILE_DIRECTORY_CACHE_SLOTS; i++) {
		if (g_directory_cache[i].valid &&
		    g_directory_cache[i].filter == g_type_filter &&
		    !strcmp(g_directory_cache[i].path, path)) {
			slot = &g_directory_cache[i];
			break;
		}
		if (!g_directory_cache[i].valid) slot = &g_directory_cache[i];
	}
	if (!slot) {
		slot = &g_directory_cache[0];
		for (int i = 1; i < FILE_DIRECTORY_CACHE_SLOTS; i++)
			if (g_directory_cache[i].last_used < slot->last_used)
				slot = &g_directory_cache[i];
	}
	LocalFileEntry *copy = count > 0
	                     ? malloc((size_t)count * sizeof(*copy)) : NULL;
	if (count > 0 && !copy) return;
	if (copy) memcpy(copy, entries, (size_t)count * sizeof(*copy));
	free(slot->entries);
	memset(slot, 0, sizeof(*slot));
	slot->entries = copy;
	slot->valid = 1;
	slot->filter = g_type_filter;
	slot->signature = signature;
	slot->last_used = ++g_directory_cache_clock;
	slot->count = count;
	slot->selected = selected >= 0 && selected < count ? selected : 0;
	slot->top = top >= 0 ? top : 0;
	snprintf(slot->path, sizeof(slot->path), "%s", path);
}

static int viewport_bottom(void) {
	int mini_top = ui_mini_player_top();
	return mini_top < 532 ? mini_top : 532;
}

static int list_visible_rows(void) {
	int rows = (viewport_bottom() - FILE_Y + 6) / FILE_ROW_H;
	if (rows < 1) rows = 1;
	if (rows > 7) rows = 7;
	return rows;
}

static int list_render_rows(void) {
	int rows = (viewport_bottom() - FILE_Y + FILE_ROW_H - 1) / FILE_ROW_H;
	if (rows < 1) rows = 1;
	if (rows > 7) rows = 7;
	return rows;
}

static int grid_visible_rows(void) {
	int rows = (viewport_bottom() - FILE_Y + FILE_GAP_Y) /
	           (FILE_CARD_H + FILE_GAP_Y);
	if (rows < 1) rows = 1;
	if (rows > 2) rows = 2;
	return rows;
}

static int grid_render_rows(void) {
	int rows = (viewport_bottom() - FILE_Y + FILE_CARD_H + FILE_GAP_Y - 1) /
	           (FILE_CARD_H + FILE_GAP_Y);
	if (rows < 1) rows = 1;
	if (rows > 3) rows = 3;
	return rows;
}

static int thumbnail_viewport_priority(int index, int selected,
	                                   int visible_first,
	                                   int visible_limit) {
	if (index == selected) return 100;
	if (index >= visible_first && index < visible_limit) {
		int distance = index > selected ? index - selected : selected - index;
		int priority = 78 - distance * 4;
		return priority > 48 ? priority : 48;
	}
	/* render_rows() includes one look-ahead row. Keep it below every visible
	 * cell so rapid scrolling never waits behind speculative work. */
	return 24;
}

static int ends_with_ci(const char *name, const char *suffix) {
	size_t name_length = strlen(name), suffix_length = strlen(suffix);
	if (name_length < suffix_length) return 0;
	name += name_length - suffix_length;
	for (size_t i = 0; i < suffix_length; i++)
		if (tolower((unsigned char)name[i]) !=
		    tolower((unsigned char)suffix[i])) return 0;
	return 1;
}

static VtLocalMediaType local_media_type(const char *name) {
	static const char *const videos[] = { ".mp4", ".m4v", ".mov", ".mkv" };
	static const char *const audio[] = {
		".mp3", ".m4a", ".aac", ".wav", ".flac"
	};
	for (unsigned int i = 0; i < sizeof(videos) / sizeof(videos[0]); i++)
		if (ends_with_ci(name, videos[i])) return VT_LOCAL_MEDIA_VIDEO;
	for (unsigned int i = 0; i < sizeof(audio) / sizeof(audio[0]); i++)
		if (ends_with_ci(name, audio[i])) return VT_LOCAL_MEDIA_AUDIO;
	if (vt_image_supported_path(name)) return VT_LOCAL_MEDIA_IMAGE;
	return 0;
}

static void find_media_artwork(VtLocalMediaItem *media) {
	if (!media || !media->type) return;
	if (media->type == VT_LOCAL_MEDIA_IMAGE) {
		snprintf(media->artwork_path, sizeof(media->artwork_path), "%s", media->path);
		media->artwork_size = media->size;
		return;
	}
	static const char *const extensions[] = { ".jpg", ".jpeg", ".png" };
	static const char *const folder_names[] = {
		"poster", "cover", "folder", "thumb", "landscape"
	};
	char base[VT_LOCAL_MEDIA_PATH_MAX];
	char directory[VT_LOCAL_MEDIA_PATH_MAX];
	snprintf(base, sizeof(base), "%s", media->path);
	char *dot = strrchr(base, '.');
	if (dot) *dot = '\0';
	snprintf(directory, sizeof(directory), "%s", media->path);
	char *slash = strrchr(directory, '/');
	if (slash) *slash = '\0';
	for (unsigned int i = 0;
	     i < sizeof(extensions) / sizeof(extensions[0]) &&
	     !media->artwork_path[0]; i++) {
		char candidate[VT_LOCAL_MEDIA_PATH_MAX];
		SceIoStat stat;
		snprintf(candidate, sizeof(candidate), "%s%s", base, extensions[i]);
		memset(&stat, 0, sizeof(stat));
		if (sceIoGetstat(candidate, &stat) >= 0) {
			snprintf(media->artwork_path, sizeof(media->artwork_path), "%s",
			         candidate);
			media->artwork_size = stat.st_size > 0 ? (uint64_t)stat.st_size : 0;
		}
	}
	for (unsigned int name = 0;
	     name < sizeof(folder_names) / sizeof(folder_names[0]) &&
	     !media->artwork_path[0]; name++) {
		for (unsigned int ext = 0;
		     ext < sizeof(extensions) / sizeof(extensions[0]); ext++) {
			char candidate[VT_LOCAL_MEDIA_PATH_MAX];
			SceIoStat stat;
			snprintf(candidate, sizeof(candidate), "%s/%s%s", directory,
			         folder_names[name], extensions[ext]);
			memset(&stat, 0, sizeof(stat));
			if (sceIoGetstat(candidate, &stat) >= 0) {
				snprintf(media->artwork_path, sizeof(media->artwork_path), "%s",
				         candidate);
				media->artwork_size = stat.st_size > 0 ? (uint64_t)stat.st_size : 0;
				break;
			}
		}
	}
}

static int entry_compare(const void *left, const void *right) {
	const LocalFileEntry *a = left, *b = right;
	if (a->is_directory != b->is_directory)
		return b->is_directory - a->is_directory;
	return strcasecmp(a->media.name, b->media.name);
}

static int add_root(LocalFileEntry *entries, int count, const char *path,
	                const char *name) {
	SceUID directory = sceIoDopen(path);
	if (directory < 0) return count;
	sceIoDclose(directory);
	LocalFileEntry *entry = &entries[count++];
	memset(entry, 0, sizeof(*entry));
	entry->is_directory = 1;
	snprintf(entry->media.path, sizeof(entry->media.path), "%s", path);
	snprintf(entry->media.name, sizeof(entry->media.name), "%s", name);
	return count;
}

static int join_path(const char *directory, const char *name,
	                 char *out, size_t out_size) {
	size_t length = strlen(directory);
	const char *separator = length && directory[length - 1] == ':' ? "" : "/";
	int written = snprintf(out, out_size, "%s%s%s", directory, separator, name);
	return written > 0 && written < (int)out_size ? 0 : -1;
}

static int load_entries(const char *path, LocalFileEntry *entries) {
	if (!path || !path[0]) {
		int count = 0;
		count = add_root(entries, count, "ux0:",
		                 vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_MEMORY_CARD));
		count = add_root(entries, count, "uma0:",
		                 vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_USB_STORAGE));
		return count;
	}
	SceUID directory = sceIoDopen(path);
	if (directory < 0) return directory;
	int count = 0;
	while (count < FILE_MAX_ENTRIES) {
		SceIoDirent source;
		memset(&source, 0, sizeof(source));
		int result = sceIoDread(directory, &source);
		if (result <= 0) break;
		/* Match Finder's default hidden-file behavior and keep Vita metadata or
		 * application-private dot entries out of the media browser. */
		if (source.d_name[0] == '.') continue;
		int is_directory = SCE_S_ISDIR(source.d_stat.st_mode);
		VtLocalMediaType type = is_directory ? 0 : local_media_type(source.d_name);
		if (!is_directory && (!type || (g_type_filter && type != g_type_filter)))
			continue;
		LocalFileEntry *entry = &entries[count];
		memset(entry, 0, sizeof(*entry));
		if (join_path(path, source.d_name, entry->media.path,
		              sizeof(entry->media.path)) < 0) continue;
		snprintf(entry->media.name, sizeof(entry->media.name), "%s", source.d_name);
		entry->media.type = type;
		entry->media.source = VT_LOCAL_MEDIA_SOURCE_FILE;
		entry->media.size = source.d_stat.st_size > 0
		                  ? (uint64_t)source.d_stat.st_size : 0;
		find_media_artwork(&entry->media);
		if (type == VT_LOCAL_MEDIA_AUDIO) {
			VtMusicMetadata metadata;
			if (vt_music_metadata_load(entry->media.path, &metadata) == 0) {
				if (metadata.title[0])
					snprintf(entry->media.name, sizeof(entry->media.name), "%s",
					         metadata.title);
				if (metadata.artist[0])
					snprintf(entry->media.artist, sizeof(entry->media.artist), "%s",
					         metadata.artist);
				if (metadata.album[0])
					snprintf(entry->media.album, sizeof(entry->media.album), "%s",
					         metadata.album);
				if (!entry->media.artwork_path[0] && metadata.artwork_path[0]) {
					snprintf(entry->media.artwork_path,
					         sizeof(entry->media.artwork_path), "%s",
					         metadata.artwork_path);
					SceIoStat artwork_stat;
					memset(&artwork_stat, 0, sizeof(artwork_stat));
					if (sceIoGetstat(metadata.artwork_path, &artwork_stat) >= 0 &&
					    artwork_stat.st_size > 0)
						entry->media.artwork_size = (uint64_t)artwork_stat.st_size;
				}
				if (metadata.duration_ms)
					entry->media.duration_ms = metadata.duration_ms;
			}
		}
		entry->is_directory = is_directory;
		count++;
	}
	sceIoDclose(directory);
	qsort(entries, (size_t)count, sizeof(*entries), entry_compare);
	return count;
}

static int load_entries_cached(const char *path, LocalFileEntry *entries,
	                           int *selected, int *top) {
	uint64_t started_us = sceKernelGetProcessTimeWide();
	uint64_t signature = directory_signature(path);
	LocalDirectoryCache *cached = directory_cache_find(path, signature);
	if (cached) {
		if (cached->count > 0)
			memcpy(entries, cached->entries,
			       (size_t)cached->count * sizeof(*entries));
		if (selected) *selected = cached->selected;
		if (top) *top = cached->top;
		log_printf("local folder cache: hit path=%s entries=%d load=%llu ms",
		           path, cached->count,
		           (unsigned long long)((sceKernelGetProcessTimeWide() -
		                                started_us) / 1000ULL));
		return cached->count;
	}
	int count = load_entries(path, entries);
	if (selected) *selected = 0;
	if (top) *top = 0;
	if (count >= 0) directory_cache_store(path, entries, count, 0, 0);
	log_printf("local folder cache: miss path=%s entries=%d load=%llu ms",
	           path && path[0] ? path : "<root>", count,
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                started_us) / 1000ULL));
	return count;
}

static void parent_path(char *path) {
	if (!path || !path[0]) return;
	char *colon = strchr(path, ':');
	char *slash = strrchr(path, '/');
	if (slash && (!colon || slash > colon)) {
		*slash = '\0';
		return;
	}
	if (colon && colon[1]) {
		colon[1] = '\0';
		return;
	}
	path[0] = '\0';
}

static void draw_icon(const LocalFileEntry *entry, int x, int y) {
	if (entry->is_directory) {
		vita2d_draw_rectangle(x + 20, y + 24, 62, 15, VT_THEME_BLUE_LIGHT);
		vita2d_draw_rectangle(x + 12, y + 35, 112, 65, VT_THEME_BLUE);
		vita2d_draw_rectangle(x + 12, y + 35, 112, 4, VT_THEME_BLUE_BRIGHT);
		return;
	}
	vita2d_draw_rectangle(x + 18, y + 14, 100, 88, VT_THEME_MEDIA_BACKDROP);
	if (entry->media.type == VT_LOCAL_MEDIA_AUDIO) {
		vita2d_draw_fill_circle(x + 68, y + 58, 30, VT_THEME_SURFACE_RAISED);
		vita2d_draw_fill_circle(x + 68, y + 58, 10, VT_THEME_BLUE_LIGHT);
	} else if (entry->media.type == VT_LOCAL_MEDIA_VIDEO) {
		for (int stripe = 0; stripe < 5; stripe++)
			vita2d_draw_rectangle(x + 18 + stripe * 20, y + 14, 10, 88,
			                      stripe & 1 ? VT_THEME_SURFACE_FOCUS
			                                 : VT_THEME_SURFACE);
		vita2d_draw_fill_circle(x + 68, y + 58, 21, VT_THEME_MEDIA_BACKDROP);
		for (int line = 0; line < 18; line++)
			vita2d_draw_rectangle(x + 62, y + 49 + line, 7 + line / 2, 1,
			                      VT_THEME_TEXT);
	} else if (entry->media.type == VT_LOCAL_MEDIA_IMAGE) {
		vita2d_draw_rectangle(x + 30, y + 23, 76, 66, VT_THEME_SURFACE_RAISED);
		vita2d_draw_fill_circle(x + 84, y + 43, 9, VT_THEME_SPECTRAL);
		for (int line = 0; line < 28; line++)
			vita2d_draw_rectangle(x + 36 + line, y + 80 - line / 2,
			                      38, 1, VT_THEME_BLUE_LIGHT);
	} else {
		vita2d_draw_rectangle(x + 34, y + 15, 68, 88, VT_THEME_SURFACE_RAISED);
		vita2d_draw_rectangle(x + 34, y + 15, 4, 88, VT_THEME_TEXT_FAINT);
		for (int line = 0; line < 4; line++)
			vita2d_draw_rectangle(x + 48, y + 40 + line * 13, 40, 2,
			                      VT_THEME_TEXT_MUTED);
	}
}

static void draw_video_preview(vita2d_texture *texture, float x, float y,
	                           float width, float height) {
	if (!texture) return;
	float tw = (float)vita2d_texture_get_width(texture);
	float th = (float)vita2d_texture_get_height(texture);
	if (tw <= 0.0f || th <= 0.0f) return;
	float source_x = 0.0f, source_y = 0.0f;
	float source_w = tw, source_h = th;
	if (tw / th > width / height) {
		source_w = th * width / height;
		source_x = (tw - source_w) * 0.5f;
	} else {
		source_h = tw * height / width;
		source_y = (th - source_h) * 0.5f;
	}
	vita2d_draw_texture_part_scale(texture, x, y, source_x, source_y,
	                               source_w, source_h,
	                               width / source_w, height / source_h);
}

static void draw_files(const LocalFileEntry *entries, int count,
	                   int selected, int top, int grid_mode,
	                   const UiFocusMotion *focus_motion) {
	ui_mini_player_pump();
	vt_video_thumbnail_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header_placeholder(NULL,
	    vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_TITLE));
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	ui_panel(FILE_X, 68, FILE_W, 34, VT_THEME_SURFACE_RAISED,
	         VT_THEME_BLUE_LIGHT, 0);
	vita2d_draw_rectangle(FILE_X + 12, 78, 3, 14, VT_THEME_SIGNAL);
	if (small) {
		char breadcrumb[256], fitted[192];
		snprintf(breadcrumb, sizeof(breadcrumb), "%s / %s",
		         vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_ROOT),
		         g_last_path[0] ? g_last_path : "");
		ui_font_fit_text(small, UI_FONT_SMALL, breadcrumb, fitted,
		                 sizeof(fitted), FILE_W - 180);
		ui_font_draw_text(small, FILE_X + 26, 91, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, fitted);
		const char *mode = vt_i18n_str(grid_mode
		    ? VT_STR_LOCAL_MEDIA_VIEW_GRID : VT_STR_LOCAL_MEDIA_VIEW_LIST);
		char hint[64];
		snprintf(hint, sizeof(hint), "R1  %s", mode);
		int width = ui_font_text_width(small, UI_FONT_SMALL, hint);
		ui_font_draw_text(small, FILE_X + FILE_W - width - 16, 91,
		                  VT_THEME_BLUE_LIGHT, UI_FONT_SMALL, hint);
	}
	int bottom = viewport_bottom();
	if (!count) {
		ui_panel(220, 202, 520, 132, VT_THEME_SURFACE,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 280, 258, VT_THEME_TEXT,
		                             UI_FONT_BODY,
		                             vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_EMPTY));
	} else if (!grid_mode) {
		ui_focus_glow_draw(focus_motion->x, focus_motion->y,
		                   focus_motion->width, focus_motion->height,
		                   sceKernelGetProcessTimeWide(), FILE_Y, bottom);
		vita2d_set_clip_rectangle(0, FILE_Y, 960, bottom);
		vita2d_enable_clipping();
		int visible_limit = top + list_visible_rows();
		for (int i = top; i < count && i < top + list_render_rows(); i++) {
			int y = FILE_Y + (i - top) * FILE_ROW_H;
			const LocalFileEntry *entry = &entries[i];
			vita2d_texture *preview = entry->media.artwork_path[0]
			    ? vt_image_thumbnail_get_priority(entry->media.artwork_path,
			          entry->media.artwork_size,
			          thumbnail_viewport_priority(i, selected, top, visible_limit))
			    : NULL;
			if (!preview && entry->media.type == VT_LOCAL_MEDIA_VIDEO)
				preview = vt_video_thumbnail_get_priority(
				    entry->media.path, entry->media.size,
				    thumbnail_viewport_priority(i, selected, top, visible_limit));
			ui_panel(FILE_X, y, FILE_W, FILE_ROW_H - 6, VT_THEME_SURFACE,
			         entry->is_directory ? VT_THEME_BLUE_LIGHT
			         : entry->media.type == VT_LOCAL_MEDIA_IMAGE ? VT_THEME_SPECTRAL
			         : entry->media.type ? VT_THEME_BLUE_BRIGHT : VT_THEME_BORDER, 0);
			if (preview) {
				vita2d_draw_rectangle(FILE_X + 9, y + 6, 70, 40,
				                      VT_THEME_MEDIA_BACKDROP);
				draw_video_preview(preview, FILE_X + 9, y + 6, 70, 40);
				vita2d_draw_rectangle(FILE_X + 9, y + 44, 70, 2, VT_THEME_COLD);
			}
			if (body) {
				char title[192];
				ui_font_fit_text(body, UI_FONT_BODY, entry->media.name, title,
				                 sizeof(title), preview ? 530 : 610);
				ui_font_draw_text(body, FILE_X + (preview ? 94 : 22), y + 33,
				                  VT_THEME_TEXT,
				                  UI_FONT_BODY, title);
			}
			if (small) {
				char detail[64];
				if (entry->is_directory)
					snprintf(detail, sizeof(detail), "%s",
					         vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_FOLDER));
				else ui_format_file_size(entry->media.size, detail, sizeof(detail));
				int width = ui_font_text_width(small, UI_FONT_SMALL, detail);
				ui_font_draw_text(small, FILE_X + FILE_W - width - 18, y + 31,
				                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL, detail);
			}
		}
		vita2d_disable_clipping();
	} else {
		ui_focus_glow_draw(focus_motion->x, focus_motion->y,
		                   focus_motion->width, focus_motion->height,
		                   sceKernelGetProcessTimeWide(), FILE_Y, bottom);
		vita2d_set_clip_rectangle(0, FILE_Y, 960, bottom);
		vita2d_enable_clipping();
		int first = top * FILE_GRID_COLS;
		int limit = (top + grid_render_rows()) * FILE_GRID_COLS;
		int visible_limit = (top + grid_visible_rows()) * FILE_GRID_COLS;
		for (int i = first; i < count && i < limit; i++) {
			int col = i % FILE_GRID_COLS;
			int row = i / FILE_GRID_COLS - top;
			int x = FILE_X + col * (FILE_CARD_W + FILE_GAP_X);
			int y = FILE_Y + row * (FILE_CARD_H + FILE_GAP_Y);
			const LocalFileEntry *entry = &entries[i];
			vita2d_texture *preview = entry->media.artwork_path[0]
			    ? vt_image_thumbnail_get_priority(entry->media.artwork_path,
			          entry->media.artwork_size,
			          thumbnail_viewport_priority(i, selected, first, visible_limit))
			    : NULL;
			if (!preview && entry->media.type == VT_LOCAL_MEDIA_VIDEO)
				preview = vt_video_thumbnail_get_priority(
				    entry->media.path, entry->media.size,
				    thumbnail_viewport_priority(i, selected, first, visible_limit));
			ui_panel(x, y, FILE_CARD_W, FILE_CARD_H, VT_THEME_SURFACE,
			         entry->is_directory ? VT_THEME_BLUE_LIGHT
			         : entry->media.type == VT_LOCAL_MEDIA_IMAGE ? VT_THEME_SPECTRAL
			         : entry->media.type ? VT_THEME_BLUE_BRIGHT : VT_THEME_BORDER, 0);
			if (preview) {
				vita2d_draw_rectangle(x + 6, y + 6, FILE_CARD_W - 12, 96,
				                      VT_THEME_MEDIA_BACKDROP);
				draw_video_preview(preview, x + 6, y + 6,
				                   FILE_CARD_W - 12, 96);
				vita2d_draw_rectangle(x + 6, y + 100,
				                      FILE_CARD_W - 12, 2, VT_THEME_COLD);
			} else {
				draw_icon(entry, x + 30, y + 2);
			}
			if (!entry->is_directory &&
			    entry->media.type == VT_LOCAL_MEDIA_VIDEO) {
				char history_id[16];
				vt_playback_history_local_id(entry->media.path, history_id);
				ui_watched_progress(history_id, x + 6, y + 96,
				                    FILE_CARD_W - 12);
			}
			if (small) {
				char title[128];
				ui_font_fit_text(small, UI_FONT_SMALL, entry->media.name, title,
				                 sizeof(title), FILE_CARD_W - 24);
				ui_font_draw_text(small, x + 12, y + 126, VT_THEME_TEXT,
				                  UI_FONT_SMALL, title);
				char detail[64];
				if (entry->is_directory)
					snprintf(detail, sizeof(detail), "%s",
					         vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_FOLDER));
				else ui_format_file_size(entry->media.size, detail, sizeof(detail));
				ui_font_draw_text(small, x + 12, y + 148, VT_THEME_TEXT_MUTED,
				                  UI_FONT_SMALL, detail);
			}
		}
		vita2d_disable_clipping();
	}
	ui_mini_player_draw();
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_local_files_screen_open(const char *root, VtLocalMediaType filter,
	                           VtLocalMediaItem *selected_out) {
	if (root) {
		if (root[0]) {
			snprintf(g_last_path, sizeof(g_last_path), "%s", root);
			snprintf(g_browser_root, sizeof(g_browser_root), "%s", root);
		} else {
			g_last_path[0] = '\0';
			g_browser_root[0] = '\0';
		}
		g_type_filter = filter;
	}
	LocalFileEntry *entries = calloc(FILE_MAX_ENTRIES, sizeof(*entries));
	if (!entries) return UI_LOCAL_MEDIA_ACTION_BACK;
	int selected = 0, top = 0;
	int count = load_entries_cached(g_last_path, entries, &selected, &top);
	if (count < 0) {
		g_last_path[0] = '\0';
		count = load_entries_cached(g_last_path, entries, &selected, &top);
	}
	int grid_mode = vt_preferences_file_browser_grid();
	UiFocusMotion focus;
	ui_focus_motion_reset(&focus);
	UiNavRepeat repeat;
	ui_nav_repeat_reset(&repeat);
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_touch_reset();
	vt_video_thumbnail_resume();
	for (;;) {
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned int pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		ui_mini_player_handle_buttons(&pressed);
		UiTouchEvent touch;
		unsigned int touch_flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(touch_flags, &touch)) touch_flags = 0;
		if (ui_mini_player_input_locked()) {
			pressed = 0;
			controls.buttons = 0;
			controls.lx = controls.ly = 128;
		}
		if (pressed & SCE_CTRL_RTRIGGER) {
			grid_mode = !grid_mode;
			vt_preferences_set_file_browser_grid(grid_mode);
			top = 0;
			ui_focus_motion_reset(&focus);
		}
		unsigned int nav = ui_nav_repeat_update(
		    &repeat, pressed, controls.buttons, controls.lx, controls.ly,
		    SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
		if (grid_mode) {
			if ((nav & SCE_CTRL_LEFT) && selected > 0) selected--;
			if ((nav & SCE_CTRL_RIGHT) && selected + 1 < count) selected++;
			if ((nav & SCE_CTRL_UP) && selected >= FILE_GRID_COLS)
				selected -= FILE_GRID_COLS;
			if ((nav & SCE_CTRL_DOWN) && selected + FILE_GRID_COLS < count)
				selected += FILE_GRID_COLS;
			int row = selected / FILE_GRID_COLS;
			if (row < top) top = row;
			if (row >= top + grid_visible_rows()) top = row - grid_visible_rows() + 1;
			if (count) ui_focus_motion_tick(
			    &focus,
			    FILE_X + (selected % FILE_GRID_COLS) * (FILE_CARD_W + FILE_GAP_X),
			    FILE_Y + (row - top) * (FILE_CARD_H + FILE_GAP_Y),
			    FILE_CARD_W, FILE_CARD_H);
		} else {
			if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
			if ((nav & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
			if (selected < top) top = selected;
			if (selected >= top + list_visible_rows()) top = selected - list_visible_rows() + 1;
			if (count) ui_focus_motion_tick(&focus, FILE_X,
			                                FILE_Y + (selected - top) * FILE_ROW_H,
			                                FILE_W, FILE_ROW_H - 6);
		}
		if ((touch_flags & UI_TOUCH_EVENT_TAP) && count) {
			int slots = grid_mode ? grid_render_rows() * FILE_GRID_COLS
			                      : list_render_rows();
			int first = grid_mode ? top * FILE_GRID_COLS : top;
			for (int slot = 0; slot < slots && first + slot < count; slot++) {
				int hit = grid_mode
				        ? ui_touch_hit_rect(touch.x, touch.y,
				              FILE_X + (slot % FILE_GRID_COLS) * (FILE_CARD_W + FILE_GAP_X),
				              FILE_Y + (slot / FILE_GRID_COLS) * (FILE_CARD_H + FILE_GAP_Y),
				              FILE_CARD_W, FILE_CARD_H)
				        : ui_touch_hit_rect(touch.x, touch.y, FILE_X,
				              FILE_Y + slot * FILE_ROW_H, FILE_W, FILE_ROW_H - 6);
				if (hit) { selected = first + slot; pressed |= SCE_CTRL_CROSS; break; }
			}
		}
		if ((pressed & SCE_CTRL_CROSS) && count) {
			LocalFileEntry *entry = &entries[selected];
			if (entry->is_directory) {
				directory_cache_store(g_last_path, entries, count, selected, top);
				snprintf(g_last_path, sizeof(g_last_path), "%s", entry->media.path);
				count = load_entries_cached(g_last_path, entries, &selected, &top);
				if (count < 0) count = 0;
				ui_focus_motion_reset(&focus);
				ui_nav_repeat_reset(&repeat);
				vt_video_thumbnail_suspend();
				vt_video_thumbnail_resume();
			} else if (entry->media.type) {
				if (selected_out) *selected_out = entry->media;
				directory_cache_store(g_last_path, entries, count, selected, top);
				vt_video_thumbnail_suspend();
				free(entries);
				return UI_LOCAL_MEDIA_ACTION_PLAY;
			}
		}
		if (pressed & SCE_CTRL_CIRCLE) {
			if (!g_last_path[0] ||
			    (g_browser_root[0] && !strcmp(g_last_path, g_browser_root))) {
				vt_video_thumbnail_suspend();
				free(entries);
				return UI_LOCAL_MEDIA_ACTION_BACK;
			}
			directory_cache_store(g_last_path, entries, count, selected, top);
			parent_path(g_last_path);
			count = load_entries_cached(g_last_path, entries, &selected, &top);
			if (count < 0) count = 0;
			ui_focus_motion_reset(&focus);
			ui_nav_repeat_reset(&repeat);
			vt_video_thumbnail_suspend();
			vt_video_thumbnail_resume();
		}
		draw_files(entries, count, selected, top, grid_mode, &focus);
		sceKernelDelayThread(1000);
	}
}

int ui_local_files_screen(VtLocalMediaItem *selected_out) {
	return ui_local_files_screen_open("", 0, selected_out);
}
