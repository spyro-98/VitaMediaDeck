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

#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/focus_glow.h"
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

typedef struct {
	VtLocalMediaItem media;
	int is_directory;
} LocalFileEntry;

static char g_last_path[VT_LOCAL_MEDIA_PATH_MAX];

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
	static const char *const audio[] = { ".mp3", ".m4a", ".aac", ".wav" };
	for (unsigned int i = 0; i < sizeof(videos) / sizeof(videos[0]); i++)
		if (ends_with_ci(name, videos[i])) return VT_LOCAL_MEDIA_VIDEO;
	for (unsigned int i = 0; i < sizeof(audio) / sizeof(audio[0]); i++)
		if (ends_with_ci(name, audio[i])) return VT_LOCAL_MEDIA_AUDIO;
	return 0;
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
		if (!strcmp(source.d_name, ".") || !strcmp(source.d_name, "..")) continue;
		int is_directory = SCE_S_ISDIR(source.d_stat.st_mode);
		VtLocalMediaType type = is_directory ? 0 : local_media_type(source.d_name);
		LocalFileEntry *entry = &entries[count];
		memset(entry, 0, sizeof(*entry));
		if (join_path(path, source.d_name, entry->media.path,
		              sizeof(entry->media.path)) < 0) continue;
		snprintf(entry->media.name, sizeof(entry->media.name), "%s", source.d_name);
		entry->media.type = type;
		entry->media.source = VT_LOCAL_MEDIA_SOURCE_FILE;
		entry->media.size = source.d_stat.st_size > 0
		                  ? (uint64_t)source.d_stat.st_size : 0;
		entry->is_directory = is_directory;
		count++;
	}
	sceIoDclose(directory);
	qsort(entries, (size_t)count, sizeof(*entries), entry_compare);
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

static void format_size(uint64_t size, char out[32]) {
	if (size >= 1024ULL * 1024ULL * 1024ULL)
		snprintf(out, 32, "%.2f GB", (double)size /
		         (1024.0 * 1024.0 * 1024.0));
	else snprintf(out, 32, "%.1f MB", (double)size / (1024.0 * 1024.0));
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
			                      stripe & 1 ? RGBA8(9, 27, 47, 255)
			                                 : RGBA8(5, 16, 29, 255));
		vita2d_draw_fill_circle(x + 68, y + 58, 21, RGBA8(2, 8, 17, 220));
		for (int line = 0; line < 18; line++)
			vita2d_draw_rectangle(x + 62, y + 49 + line, 7 + line / 2, 1,
			                      VT_THEME_TEXT);
	} else {
		vita2d_draw_rectangle(x + 34, y + 15, 68, 88, VT_THEME_SURFACE_RAISED);
		vita2d_draw_rectangle(x + 34, y + 15, 4, 88, VT_THEME_TEXT_FAINT);
		for (int line = 0; line < 4; line++)
			vita2d_draw_rectangle(x + 48, y + 40 + line * 13, 40, 2,
			                      VT_THEME_TEXT_MUTED);
	}
}

static void draw_files(const LocalFileEntry *entries, int count,
	                   int selected, int top, int grid_mode,
	                   const UiFocusMotion *focus_motion) {
	ui_mini_player_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header_placeholder(NULL,
	    vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_TITLE));
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	ui_panel(FILE_X, 68, FILE_W, 34, VT_THEME_SURFACE,
	         VT_THEME_BLUE_LIGHT, 0);
	if (small) {
		char breadcrumb[256], fitted[192];
		snprintf(breadcrumb, sizeof(breadcrumb), "%s / %s",
		         vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_ROOT),
		         g_last_path[0] ? g_last_path : "");
		ui_font_fit_text(small, UI_FONT_SMALL, breadcrumb, fitted,
		                 sizeof(fitted), FILE_W - 180);
		ui_font_draw_text(small, FILE_X + 16, 91, VT_THEME_TEXT_MUTED,
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
		for (int i = top; i < count && i < top + list_render_rows(); i++) {
			int y = FILE_Y + (i - top) * FILE_ROW_H;
			const LocalFileEntry *entry = &entries[i];
			ui_panel(FILE_X, y, FILE_W, FILE_ROW_H - 6, VT_THEME_SURFACE,
			         entry->is_directory ? VT_THEME_BLUE_LIGHT
			         : entry->media.type ? VT_THEME_BLUE_BRIGHT : VT_THEME_BORDER, 0);
			if (body) {
				char title[192];
				ui_font_fit_text(body, UI_FONT_BODY, entry->media.name, title,
				                 sizeof(title), 610);
				ui_font_draw_text(body, FILE_X + 22, y + 33, VT_THEME_TEXT,
				                  UI_FONT_BODY, title);
			}
			if (small) {
				char detail[64];
				if (entry->is_directory)
					snprintf(detail, sizeof(detail), "%s",
					         vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_FOLDER));
				else format_size(entry->media.size, detail);
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
		for (int i = first; i < count && i < limit; i++) {
			int col = i % FILE_GRID_COLS;
			int row = i / FILE_GRID_COLS - top;
			int x = FILE_X + col * (FILE_CARD_W + FILE_GAP_X);
			int y = FILE_Y + row * (FILE_CARD_H + FILE_GAP_Y);
			const LocalFileEntry *entry = &entries[i];
			ui_panel(x, y, FILE_CARD_W, FILE_CARD_H, VT_THEME_SURFACE,
			         entry->is_directory ? VT_THEME_BLUE_LIGHT
			         : entry->media.type ? VT_THEME_BLUE_BRIGHT : VT_THEME_BORDER, 0);
			draw_icon(entry, x + 30, y + 2);
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
				else format_size(entry->media.size, detail);
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

int ui_local_files_screen(VtLocalMediaItem *selected_out) {
	LocalFileEntry *entries = calloc(FILE_MAX_ENTRIES, sizeof(*entries));
	if (!entries) return UI_LOCAL_MEDIA_ACTION_BACK;
	int count = load_entries(g_last_path, entries);
	if (count < 0) { g_last_path[0] = '\0'; count = load_entries(g_last_path, entries); }
	int selected = 0, top = 0;
	int grid_mode = vt_preferences_file_browser_grid();
	UiFocusMotion focus;
	ui_focus_motion_reset(&focus);
	UiNavRepeat repeat;
	ui_nav_repeat_reset(&repeat);
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_touch_reset();
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
				snprintf(g_last_path, sizeof(g_last_path), "%s", entry->media.path);
				count = load_entries(g_last_path, entries);
				if (count < 0) count = 0;
				selected = top = 0;
				ui_focus_motion_reset(&focus);
				ui_nav_repeat_reset(&repeat);
			} else if (entry->media.type) {
				if (selected_out) *selected_out = entry->media;
				free(entries);
				return UI_LOCAL_MEDIA_ACTION_PLAY;
			}
		}
		if (pressed & SCE_CTRL_CIRCLE) {
			if (!g_last_path[0]) {
				free(entries);
				return UI_LOCAL_MEDIA_ACTION_BACK;
			}
			parent_path(g_last_path);
			count = load_entries(g_last_path, entries);
			if (count < 0) count = 0;
			selected = top = 0;
			ui_focus_motion_reset(&focus);
			ui_nav_repeat_reset(&repeat);
		}
		draw_files(entries, count, selected, top, grid_mode, &focus);
		sceKernelDelayThread(1000);
	}
}
