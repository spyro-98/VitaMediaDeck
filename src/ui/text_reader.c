#include "ui/text_reader.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define TEXT_READER_MAX_LINES 8192
#define TEXT_READER_VISIBLE_LINES 17
#define TEXT_READER_TABBED_VISIBLE_LINES 16
#define TEXT_READER_LINE_HEIGHT 24
#define TEXT_READER_TOP 108
#define TEXT_READER_TABBED_TOP 144
#define TEXT_READER_WIDTH 872
#define TEXT_READER_TAB_X 36
#define TEXT_READER_TAB_Y 72
#define TEXT_READER_TAB_W 178
#define TEXT_READER_TAB_H 42
#define TEXT_READER_TAB_GAP 12

typedef struct {
	int offset;
	int length;
} TextReaderLine;

static TextReaderLine g_reader_lines[TEXT_READER_MAX_LINES];
static int g_reader_line_count;

static size_t utf8_character_length(const char *text) {
	unsigned char lead = (unsigned char)text[0];
	size_t length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2
	              : (lead & 0xF0) == 0xE0 ? 3
	              : (lead & 0xF8) == 0xF0 ? 4 : 1;
	for (size_t i = 1; i < length; i++)
		if (!text[i] || ((unsigned char)text[i] & 0xc0U) != 0x80U) return 1;
	return length;
}

static void index_text(vita2d_font *font, const char *text) {
	g_reader_line_count = 0;
	if (!font || !text) return;
	int offset = 0;
	while (text[offset] && g_reader_line_count < TEXT_READER_MAX_LINES) {
		while (text[offset] == '\r' || text[offset] == '\n') {
			g_reader_lines[g_reader_line_count++] = (TextReaderLine){ offset, 0 };
			if (text[offset] == '\r' && text[offset + 1] == '\n') offset++;
			offset++;
			if (g_reader_line_count >= TEXT_READER_MAX_LINES) return;
		}
		if (!text[offset]) break;
		int begin = offset;
		int last_space = -1;
		char measured[512];
		int measured_length = 0;
		while (text[offset] && text[offset] != '\r' && text[offset] != '\n') {
			size_t char_length = utf8_character_length(text + offset);
			if (measured_length + (int)char_length >= (int)sizeof(measured)) break;
			memcpy(measured + measured_length, text + offset, char_length);
			measured_length += (int)char_length;
			measured[measured_length] = '\0';
			if (text[offset] == ' ') last_space = offset;
			if (ui_font_text_width(font, UI_FONT_SMALL, measured) > TEXT_READER_WIDTH) {
				if (last_space >= begin) offset = last_space;
				break;
			}
			offset += (int)char_length;
		}
		if (offset <= begin) offset = begin + 1;
		g_reader_lines[g_reader_line_count++] =
		    (TextReaderLine){ begin, offset - begin };
		while (text[offset] == ' ') offset++;
		if (text[offset] == '\r') offset++;
		if (text[offset] == '\n') offset++;
	}
}

static void draw_reader_tabs(vita2d_font *font, const char *parent_title,
	                         const char *title) {
	const char *labels[2] = { parent_title, title };
	for (int i = 0; i < 2; i++) {
		int x = TEXT_READER_TAB_X + i * (TEXT_READER_TAB_W + TEXT_READER_TAB_GAP);
		ui_panel(x, TEXT_READER_TAB_Y, TEXT_READER_TAB_W,
		         TEXT_READER_TAB_H,
		         i == 1 ? VT_THEME_SURFACE_RAISED : VT_THEME_SURFACE,
		         i == 1 ? VT_THEME_SIGNAL_LIGHT : VT_THEME_BORDER_DIM, 0);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		if (small) {
			char index[4];
			snprintf(index, sizeof(index), "%02d", i + 1);
			ui_font_draw_text(small, x + 12, TEXT_READER_TAB_Y + 28,
			                  i == 1 ? VT_THEME_SIGNAL_LIGHT : VT_THEME_TEXT_FAINT,
			                  UI_FONT_SMALL, index);
		}
		if (font && labels[i])
			ui_font_draw_text(font, x + 46, TEXT_READER_TAB_Y + 29,
			                  i == 1 ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
			                  UI_FONT_BODY, labels[i]);
	}
}

static void draw_reader(const char *parent_title, const char *title,
	                    const char *text, int first_line) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	int tabbed = parent_title && parent_title[0];
	int visible_lines = tabbed ? TEXT_READER_TABBED_VISIBLE_LINES
	                           : TEXT_READER_VISIBLE_LINES;
	int text_top = tabbed ? TEXT_READER_TABBED_TOP : TEXT_READER_TOP;
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header(NULL);
	if (tabbed)
		draw_reader_tabs(body, parent_title, title);
	else if (body)
		ui_font_draw_text(body, 44, 91, VT_THEME_TEXT, UI_FONT_BODY,
		                       title && title[0] ? title : "VitaMediaDeck");
	ui_panel(32, tabbed ? 124 : 98, 896, tabbed ? 394 : 420,
	         VT_THEME_SURFACE, VT_THEME_BLUE_BRIGHT, 0);
	vita2d_set_clip_rectangle(40, tabbed ? 122 : 96, 920, 522);
	vita2d_enable_clipping();
	for (int row = 0; row < visible_lines; row++) {
		int index = first_line + row;
		if (index >= g_reader_line_count) break;
		TextReaderLine line = g_reader_lines[index];
		if (!line.length) continue;
		char rendered[512];
		int length = line.length;
		if (length >= (int)sizeof(rendered)) length = sizeof(rendered) - 1;
		memcpy(rendered, text + line.offset, length);
		rendered[length] = '\0';
		if (small)
			ui_font_draw_text(small, 44,
				                       text_top + row * TEXT_READER_LINE_HEIGHT,
			                       VT_THEME_TEXT, UI_FONT_SMALL, rendered);
	}
	vita2d_disable_clipping();
	if (g_reader_line_count > visible_lines) {
		float track_y = tabbed ? 134.0f : 106.0f;
		float track_h = tabbed ? 374.0f : 402.0f;
		float thumb_h = track_h * (float)visible_lines /
		                (float)g_reader_line_count;
		if (thumb_h < 28.0f) thumb_h = 28.0f;
		int max_first = g_reader_line_count - visible_lines;
		float thumb_y = track_y + (track_h - thumb_h) *
		                (max_first > 0 ? (float)first_line / (float)max_first : 0.0f);
		vita2d_draw_rectangle(930, track_y, 4, track_h, VT_THEME_SURFACE_FOCUS);
		vita2d_draw_rectangle(930, thumb_y, 4, thumb_h, VT_THEME_BLUE_LIGHT);
	}
	ui_brand_draw_status_indicators();
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

static void text_reader_run_internal(const char *parent_title, const char *title,
	                                 const char *text) {
	if (!text || !text[0]) return;
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	index_text(small, text);
	if (g_reader_line_count <= 0) return;
	int tabbed = parent_title && parent_title[0];
	int visible_lines = tabbed ? TEXT_READER_TABBED_VISIBLE_LINES
	                           : TEXT_READER_VISIBLE_LINES;
	int first_line = 0;
	int max_first = g_reader_line_count > visible_lines
	              ? g_reader_line_count - visible_lines : 0;
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	UiNavRepeat nav_repeat;
	ui_nav_repeat_reset(&nav_repeat);
	int touch_y = 0;
	ui_touch_reset();
	for (;;) {
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		if (pressed & SCE_CTRL_CIRCLE) break;
		if (tabbed && (pressed & (SCE_CTRL_RTRIGGER | SCE_CTRL_LEFT))) break;
		unsigned int nav = ui_nav_repeat_update(
		    &nav_repeat, pressed, ctrl.buttons, ctrl.lx, ctrl.ly,
		    SCE_CTRL_UP | SCE_CTRL_DOWN);
		if ((nav & SCE_CTRL_UP) && first_line > 0) first_line--;
		if ((nav & SCE_CTRL_DOWN) && first_line < max_first) first_line++;
		UiTouchEvent touch;
		unsigned int flags = ui_touch_poll(&touch);
		if (tabbed && (flags & UI_TOUCH_EVENT_TAP) &&
		    ui_touch_hit_rect(touch.x, touch.y, TEXT_READER_TAB_X,
		                      TEXT_READER_TAB_Y, TEXT_READER_TAB_W,
		                      TEXT_READER_TAB_H))
			break;
		if ((flags & UI_TOUCH_EVENT_TAP) && touch.x >= 900) {
			int track_top = tabbed ? 134 : 106;
			int track_height = tabbed ? 374 : 402;
			if (touch.y >= track_top && touch.y < track_top + track_height &&
			    max_first > 0) {
				first_line = (touch.y - track_top) * max_first / track_height;
				if (first_line < 0) first_line = 0;
				if (first_line > max_first) first_line = max_first;
			}
		}
		if (flags & UI_TOUCH_EVENT_DOWN) touch_y = touch.y;
		if (flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD)) {
			int delta = touch_y - touch.y;
			if (delta >= TEXT_READER_LINE_HEIGHT || delta <= -TEXT_READER_LINE_HEIGHT) {
				first_line += delta / TEXT_READER_LINE_HEIGHT;
				if (first_line < 0) first_line = 0;
				if (first_line > max_first) first_line = max_first;
				touch_y = touch.y;
			}
		}
		draw_reader(parent_title, title, text, first_line);
		sceKernelDelayThread(vt_preferences_reduce_motion() ? 16000 : 1000);
	}
	ui_touch_reset();
}

void ui_text_reader_run(const char *title, const char *text) {
	text_reader_run_internal(NULL, title, text);
}

void ui_text_reader_run_tabbed(const char *parent_title, const char *title,
	                           const char *text) {
	text_reader_run_internal(parent_title, title, text);
}
