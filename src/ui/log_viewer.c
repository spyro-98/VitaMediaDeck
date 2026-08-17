#include "ui/log_viewer.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "app_paths.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define LOG_TAIL_MAX (48 * 1024)
#define LOG_LINE_MAX_CHARS 88
#define LOG_MAX_LINES 2048
#define LOG_VISIBLE_LINES 16
#define TAB_Y             (UI_BRAND_HEADER_HEIGHT + 10)
#define TAB_H             40
#define TAB_BASELINE_Y    (TAB_Y + 27)
#define TAB_UNDERLINE_Y   (TAB_Y + TAB_H - 3)
#define LOG_LIST_Y        (UI_BRAND_HEADER_HEIGHT + 70)
#define LOG_TITLE_Y       (UI_BRAND_HEADER_HEIGHT + 81)
#define LOG_CLIP_TOP      (UI_BRAND_HEADER_HEIGHT + 94)
#define LOG_TEXT_Y        (UI_BRAND_HEADER_HEIGHT + 112)
#define LOG_ROW_H 58

#define COLOR_TEXT  VT_THEME_TEXT
#define COLOR_MUTED VT_THEME_TEXT_MUTED
#define COLOR_BLUE  VT_THEME_BLUE_BRIGHT
#define COLOR_CYAN  VT_THEME_BLUE_LIGHT
#define COLOR_CARD  VT_THEME_SURFACE

typedef struct {
	VtStringId label;
	const char *path;
} LogEntry;

typedef struct {
	int offset;
	int length;
} LogLine;

static const LogEntry g_entries[] = {
	{ VT_STR_ABOUT_LOG_SESSION, VITATUBE_SESSION_LOG_PATH }
};

static char g_log_text[LOG_TAIL_MAX + 1];
static LogLine g_log_lines[LOG_MAX_LINES];
static int g_log_line_count;
static int g_log_truncated;

static const char *log_label(int index) {
	if (index < 0 || index >= (int)(sizeof(g_entries) / sizeof(g_entries[0])))
		return "";
	return vt_i18n_str(g_entries[index].label);
}

static void format_size(uint64_t size, char out[32]) {
	if (size < 1024ULL) snprintf(out, 32, "%llu B", (unsigned long long)size);
	else if (size < 1024ULL * 1024ULL)
		snprintf(out, 32, "%llu KB", (unsigned long long)(size / 1024ULL));
	else
		snprintf(out, 32, "%llu.%llu MB",
		         (unsigned long long)(size / (1024ULL * 1024ULL)),
		         (unsigned long long)((size * 10ULL / (1024ULL * 1024ULL)) % 10ULL));
}

static int log_size(int index, uint64_t *size) {
	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	if (index < 0 || index >= (int)(sizeof(g_entries) / sizeof(g_entries[0])) ||
	    sceIoGetstat(g_entries[index].path, &stat) < 0) {
		if (size) *size = 0;
		return 0;
	}
	if (size) *size = stat.st_size;
	return stat.st_size > 0;
}

static void index_log_lines(int start) {
	g_log_line_count = 0;
	int pos = start;
	int length = (int)strlen(g_log_text);
	while (pos < length && g_log_line_count < LOG_MAX_LINES) {
		while (pos < length && (g_log_text[pos] == '\n' || g_log_text[pos] == '\r')) pos++;
		if (pos >= length) break;
		int begin = pos;
		int chars = 0;
		while (pos < length && g_log_text[pos] != '\n' && g_log_text[pos] != '\r' &&
		       chars < LOG_LINE_MAX_CHARS) {
			pos++;
			chars++;
		}
		g_log_lines[g_log_line_count].offset = begin;
		g_log_lines[g_log_line_count].length = pos - begin;
		g_log_line_count++;
	}
}

static int load_log(int index) {
	g_log_text[0] = '\0';
	g_log_line_count = 0;
	g_log_truncated = 0;
	uint64_t size = 0;
	if (!log_size(index, &size)) return -1;
	SceUID fd = sceIoOpen(g_entries[index].path, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	uint64_t offset = size > LOG_TAIL_MAX ? size - LOG_TAIL_MAX : 0;
	if (offset) {
		g_log_truncated = 1;
		sceIoLseek(fd, (SceOff)offset, SCE_SEEK_SET);
	}
	int total = 0;
	while (total < LOG_TAIL_MAX) {
		int n = sceIoRead(fd, g_log_text + total, LOG_TAIL_MAX - total);
		if (n <= 0) break;
		total += n;
	}
	sceIoClose(fd);
	g_log_text[total] = '\0';
	int start = 0;
	if (offset) {
		while (start < total && g_log_text[start] != '\n') start++;
		if (start < total) start++;
	}
	index_log_lines(start);
	return g_log_line_count > 0 ? 0 : -1;
}

static void draw_tabs(void) {
	vita2d_font *font = ui_runtime_font(UI_FONT_BODY);
	const char *labels[2] = {
		vt_i18n_str(VT_STR_ABOUT_TAB_SPECS),
		vt_i18n_str(VT_STR_ABOUT_TAB_LOGS)
	};
	for (int i = 0; i < 2; i++) {
		int x = 36 + i * 190;
		vita2d_draw_rectangle(x, TAB_Y, 178, TAB_H,
		                      i == 1 ? RGBA8(13, 55, 94, 255) : COLOR_CARD);
		if (i == 1) vita2d_draw_rectangle(x, TAB_UNDERLINE_Y, 178, 3, COLOR_CYAN);
		if (font) ui_font_draw_text(font, x + 18, TAB_BASELINE_Y,
		                                 i == 1 ? COLOR_TEXT : COLOR_MUTED,
		                                 UI_FONT_BODY, labels[i]);
	}
}

static void draw_log_screen(int cursor, int viewing, int first_line,
	                        const UiSectionsSidebar *sidebar) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_brand_draw_header(NULL);
	draw_tabs();
	if (!viewing) {
		for (int i = 0; i < (int)(sizeof(g_entries) / sizeof(g_entries[0])); i++) {
			int y = LOG_LIST_Y + i * LOG_ROW_H;
			vita2d_draw_rectangle(36, y, 888, 48,
			                      i == cursor ? RGBA8(15, 45, 78, 255) : COLOR_CARD);
			vita2d_draw_rectangle(36, y, 4, 48, i == cursor ? COLOR_CYAN : COLOR_BLUE);
			uint64_t size = 0;
			int available = log_size(i, &size);
			if (body) ui_font_draw_text(body, 58, y + 30,
			                               available ? COLOR_TEXT : COLOR_MUTED,
			                               UI_FONT_BODY, log_label(i));
			if (small) {
				char detail[48];
				if (available) format_size(size, detail);
				else snprintf(detail, sizeof(detail), "%s",
				              vt_i18n_str(VT_STR_ABOUT_LOG_NOT_AVAILABLE));
				int width = ui_font_text_width(small, UI_FONT_SMALL, detail);
				ui_font_draw_text(small, 900 - width, y + 29, COLOR_MUTED,
				                      UI_FONT_SMALL, detail);
			}
		}
		if (small) ui_font_draw_text(
		    small, 40, 507,
		    vt_preferences_disk_logs_enabled() ? COLOR_MUTED : VT_THEME_WARNING,
		    UI_FONT_SMALL,
		    vt_preferences_disk_logs_enabled()
		        ? vt_i18n_str(VT_STR_ABOUT_LOG_OPEN_HINT)
		        : vt_i18n_str(VT_STR_ABOUT_LOG_WRITES_DISABLED));
	} else {
		if (body) ui_font_draw_text(body, 40, LOG_TITLE_Y, COLOR_TEXT, UI_FONT_BODY,
		                               log_label(cursor));
		if (g_log_truncated && small)
			ui_font_draw_text(small, 620, LOG_TITLE_Y, VT_THEME_WARNING,
			                      UI_FONT_SMALL,
			                      vt_i18n_str(VT_STR_ABOUT_LOG_TAIL_ONLY));
		vita2d_set_clip_rectangle(36, LOG_CLIP_TOP, 924, 478);
		vita2d_enable_clipping();
		for (int row = 0; row < LOG_VISIBLE_LINES; row++) {
			int line_index = first_line + row;
			if (line_index >= g_log_line_count) break;
			LogLine line = g_log_lines[line_index];
			char text[LOG_LINE_MAX_CHARS + 1];
			int len = line.length;
			if (len > LOG_LINE_MAX_CHARS) len = LOG_LINE_MAX_CHARS;
			memcpy(text, g_log_text + line.offset, len);
			text[len] = '\0';
			if (small) ui_font_draw_text(small, 40, LOG_TEXT_Y + row * 20,
			                                 COLOR_MUTED, UI_FONT_SMALL, text);
		}
		vita2d_disable_clipping();
		if (small) ui_font_draw_text(small, 40, 507, COLOR_MUTED,
		                                 UI_FONT_SMALL,
		                                 vt_i18n_str(VT_STR_ABOUT_LOG_VIEW_HINT));
	}
	if (sidebar && sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar->cursor, sidebar->animation,
		                         sidebar->focus_cursor);
	ui_mini_player_draw();
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_log_viewer_screen(void) {
	int cursor = 0;
	int viewing = 0;
	int first_line = 0;
	int direction_state = 0;
	uint64_t next_repeat_us = 0;
	int drag_active = 0, drag_y = 0, drag_line = 0;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_INFO);
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_touch_reset();
	for (;;) {
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		ui_mini_player_handle_buttons(&pressed);
		if (ui_mini_player_input_locked()) {
			pressed = 0;
			ctrl.buttons &= SCE_CTRL_SELECT;
			ctrl.lx = ctrl.ly = ctrl.rx = ctrl.ry = 128;
		}
		UiTouchEvent touch;
		unsigned int touch_flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(touch_flags, &touch))
			touch_flags = UI_TOUCH_EVENT_NONE;
		int was_open = sidebar.open;
		int section = ui_sections_sidebar_handle_buttons(
		    &sidebar, &pressed, ctrl.buttons, ctrl.ly);
		if (sidebar.open || was_open) {
			int touched = ui_sections_sidebar_handle_touch(
			    &sidebar, touch_flags, touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) return section;

		if (!viewing && !sidebar.open &&
		    ((pressed & SCE_CTRL_LEFT) || ctrl.lx < 48 ||
		     ((touch_flags & UI_TOUCH_EVENT_TAP) &&
		      ui_touch_hit_rect(touch.x, touch.y, 36, TAB_Y, 178, TAB_H))))
			return UI_LOG_VIEWER_TO_SPECS;
		if (pressed & SCE_CTRL_CIRCLE) {
			if (viewing) { viewing = 0; first_line = 0; }
			else return UI_SECTION_NONE;
		}

		uint64_t now = sceKernelGetProcessTimeWide();
		int direction = 0;
		if (!sidebar.open) {
			if (ctrl.buttons & SCE_CTRL_UP) direction = -1;
			else if (ctrl.buttons & SCE_CTRL_DOWN) direction = 1;
			else if (ctrl.ly < 48) direction = -1;
			else if (ctrl.ly > 207) direction = 1;
		}
		int navigate = 0;
		if (!direction) direction_state = 0;
		else if (direction != direction_state) {
			direction_state = direction;
			next_repeat_us = now + 280000ULL;
			navigate = 1;
		} else if (now >= next_repeat_us) {
			next_repeat_us = now + 95000ULL;
			navigate = 1;
		}
		if (navigate) {
			if (viewing) {
				first_line += direction;
				int max_first = g_log_line_count > LOG_VISIBLE_LINES
				              ? g_log_line_count - LOG_VISIBLE_LINES : 0;
				if (first_line < 0) first_line = 0;
				if (first_line > max_first) first_line = max_first;
			} else {
				cursor += direction;
				if (cursor < 0) cursor = 0;
				if (cursor >= (int)(sizeof(g_entries) / sizeof(g_entries[0])))
					cursor = (int)(sizeof(g_entries) / sizeof(g_entries[0])) - 1;
			}
		}
		if (!viewing && (pressed & SCE_CTRL_CROSS) && load_log(cursor) == 0) {
			viewing = 1;
			first_line = g_log_line_count > LOG_VISIBLE_LINES
			           ? g_log_line_count - LOG_VISIBLE_LINES : 0;
		}
		if (viewing && (pressed & SCE_CTRL_TRIANGLE) && load_log(cursor) == 0)
			first_line = g_log_line_count > LOG_VISIBLE_LINES
			           ? g_log_line_count - LOG_VISIBLE_LINES : 0;

		if (!viewing && (touch_flags & UI_TOUCH_EVENT_TAP)) {
			for (int i = 0; i < (int)(sizeof(g_entries) / sizeof(g_entries[0])); i++) {
				if (!ui_touch_hit_rect(touch.x, touch.y, 36,
				                       LOG_LIST_Y + i * LOG_ROW_H, 888, 48)) continue;
				cursor = i;
				if (load_log(cursor) == 0) {
					viewing = 1;
					first_line = g_log_line_count > LOG_VISIBLE_LINES
					           ? g_log_line_count - LOG_VISIBLE_LINES : 0;
				}
			}
		}
		if (viewing && (touch_flags & UI_TOUCH_EVENT_DOWN)) {
			drag_active = 1;
			drag_y = touch.y;
			drag_line = first_line;
		}
		if (viewing && drag_active &&
		    (touch_flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD))) {
			first_line = drag_line + (drag_y - touch.y) / 20;
			int max_first = g_log_line_count > LOG_VISIBLE_LINES
			              ? g_log_line_count - LOG_VISIBLE_LINES : 0;
			if (first_line < 0) first_line = 0;
			if (first_line > max_first) first_line = max_first;
		}
		if (touch_flags & UI_TOUCH_EVENT_UP) drag_active = 0;

		draw_log_screen(cursor, viewing, first_line, &sidebar);
	}
}
