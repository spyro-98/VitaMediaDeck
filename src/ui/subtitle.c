#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
#include "ui/subtitle.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vita2d.h>
#include "settings/preferences.h"
#include "ui/runtime.h"
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

static unsigned subtitle_text_color(void) {
	switch (vt_preferences_subtitle_text_color()) {
		case VT_SUBTITLE_TEXT_YELLOW: return RGBA8(255, 232, 96, 255);
		case VT_SUBTITLE_TEXT_CYAN: return RGBA8(108, 235, 255, 255);
		case VT_SUBTITLE_TEXT_GREEN: return RGBA8(124, 244, 148, 255);
		case VT_SUBTITLE_TEXT_ORANGE: return RGBA8(255, 174, 72, 255);
		case VT_SUBTITLE_TEXT_MAGENTA: return RGBA8(255, 120, 218, 255);
		case VT_SUBTITLE_TEXT_BLUE: return RGBA8(130, 174, 255, 255);
		case VT_SUBTITLE_TEXT_GRAY: return RGBA8(196, 204, 212, 255);
		default: return RGBA8(255, 255, 255, 255);
	}
}

static unsigned subtitle_border_color(void) {
	switch (vt_preferences_subtitle_border_color()) {
		case VT_SUBTITLE_BORDER_MIDNIGHT: return RGBA8(3, 18, 34, 255);
		case VT_SUBTITLE_BORDER_WHITE: return RGBA8(255, 255, 255, 255);
		case VT_SUBTITLE_BORDER_YELLOW: return RGBA8(255, 210, 48, 255);
		case VT_SUBTITLE_BORDER_CYAN: return RGBA8(28, 218, 235, 255);
		case VT_SUBTITLE_BORDER_ORANGE: return RGBA8(242, 116, 44, 255);
		case VT_SUBTITLE_BORDER_BLUE: return RGBA8(46, 112, 232, 255);
		case VT_SUBTITLE_BORDER_GRAY: return RGBA8(90, 102, 112, 255);
		default: return RGBA8(0, 0, 0, 255);
	}
}

static unsigned subtitle_background_color(void) {
	switch (vt_preferences_subtitle_background_color()) {
		case VT_SUBTITLE_BACKGROUND_BLACK: return RGBA8(0, 0, 0, 198);
		case VT_SUBTITLE_BACKGROUND_MIDNIGHT: return RGBA8(3, 18, 34, 210);
		case VT_SUBTITLE_BACKGROUND_WHITE: return RGBA8(255, 255, 255, 205);
		case VT_SUBTITLE_BACKGROUND_NAVY: return RGBA8(3, 26, 52, 214);
		case VT_SUBTITLE_BACKGROUND_BURGUNDY: return RGBA8(50, 5, 18, 214);
		case VT_SUBTITLE_BACKGROUND_FOREST: return RGBA8(4, 38, 28, 214);
		case VT_SUBTITLE_BACKGROUND_SMOKE: return RGBA8(38, 45, 50, 205);
		default: return 0;
	}
}

static int subtitle_max_width(void) {
	switch (vt_preferences_subtitle_max_width()) {
		case VT_SUBTITLE_WIDTH_60: return SCREEN_WIDTH * 60 / 100;
		case VT_SUBTITLE_WIDTH_75: return SCREEN_WIDTH * 75 / 100;
		case VT_SUBTITLE_WIDTH_96: return SCREEN_WIDTH * 96 / 100;
		default: return SCREEN_WIDTH * 88 / 100;
	}
}

static unsigned subtitle_font_size(void) {
	return vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_SMALL
	     ? UI_FONT_SMALL
	     : vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_MEDIUM_LARGE
	     ? UI_FONT_SUBTITLE_MEDIUM_LARGE
	     : vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_LARGE
	     ? UI_FONT_SUBTITLE_LARGE
	     : vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_EXTRA_LARGE
	     ? UI_FONT_SUBTITLE_EXTRA_LARGE : UI_FONT_BODY;
}

static size_t subtitle_utf8_bytes(const char *text) {
	const unsigned char *s = (const unsigned char *)text;
	if (!s || !s[0]) return 0;
	if (s[0] < 0x80U) return 1;
	size_t bytes = s[0] >= 0xC2U && s[0] <= 0xDFU ? 2
	             : s[0] >= 0xE0U && s[0] <= 0xEFU ? 3
	             : s[0] >= 0xF0U && s[0] <= 0xF4U ? 4 : 1;
	for (size_t i = 1; i < bytes; i++)
		if (!s[i] || (s[i] & 0xC0U) != 0x80U) return 1;
	return bytes;
}

static size_t subtitle_trim_last_utf8(char *text, size_t length) {
	if (!text || !length) return 0;
	length--;
	while (length && ((unsigned char)text[length] & 0xC0U) == 0x80U)
		length--;
	text[length] = '\0';
	return length;
}

static void subtitle_add_ellipsis(vita2d_font *font, unsigned size,
	                              char line[256], int max_width) {
	static const char ellipsis[] = "\xE2\x80\xA6";
	size_t length = strlen(line);
	char candidate[256];
	for (;;) {
		snprintf(candidate, sizeof(candidate), "%s%s", line, ellipsis);
		if (!line[0] || ui_font_text_width(font, size, candidate) <= max_width)
			break;
		length = subtitle_trim_last_utf8(line, length);
		while (length && line[length - 1] == ' ')
			line[--length] = '\0';
	}
	snprintf(line, 256, "%s", candidate);
}

static int wrap_subtitle(vita2d_font *font, unsigned size, const char *text,
	                     int max_width, int max_lines, char lines[4][256]) {
	if (max_lines < 1) max_lines = 1;
	if (max_lines > 4) max_lines = 4;
	const char *cursor = text ? text : "";
	int count = 0;
	while (*cursor && count < max_lines) {
		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r') cursor++;
		if (*cursor == '\n') { cursor++; continue; }
		char *line = lines[count];
		size_t used = 0;
		size_t last_break = 0;
		const char *after_break = NULL;
		while (*cursor) {
			if (*cursor == '\r') { cursor++; continue; }
			if (*cursor == '\n') { cursor++; break; }
			size_t bytes = subtitle_utf8_bytes(cursor);
			if (!bytes) break;
			int is_space = *cursor == ' ' || *cursor == '\t';
			if (is_space && used == 0) { cursor += bytes; continue; }
			size_t before = used;
			if (is_space) {
				if (used + 1 >= 256) break;
				line[used++] = ' ';
				last_break = before;
				after_break = cursor + bytes;
			} else {
				if (used + bytes >= 256) break;
				memcpy(line + used, cursor, bytes);
				used += bytes;
			}
			line[used] = '\0';
			if (ui_font_text_width(font, size, line) > max_width && before > 0) {
				if (after_break && last_break > 0) {
					used = last_break;
					cursor = after_break;
				} else {
					used = before;
				}
				line[used] = '\0';
				break;
			}
			cursor += bytes;
		}
		while (used && line[used - 1] == ' ') line[--used] = '\0';
		if (used) count++;
	}
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
	       *cursor == '\n') cursor++;
	if (*cursor && count > 0) subtitle_add_ellipsis(font, size,
	                                               lines[count - 1], max_width);
	return count;
}

void ui_subtitle_draw(const char *text, float vx, float vy, float vw, float vh) {
	if (!text || !text[0] || vw <= 0 || vh <= 0) return;
	float scale = fminf(vw / 960.0f, vh / 544.0f);
	unsigned size = (unsigned)fmaxf(1.0f, roundf(subtitle_font_size() * scale));
	vita2d_font *font = scale < 0.99f
	    ? ui_runtime_subtitle_scaled_font(vt_preferences_subtitle_font(), size)
	    : ui_runtime_subtitle_font(vt_preferences_subtitle_font(), size);
	char lines[4][256];
	memset(lines, 0, sizeof(lines));
	int max_width = (int)(subtitle_max_width() * scale);
	int count = wrap_subtitle(font, size, text, max_width,
	                          vt_preferences_subtitle_max_rows(), lines);
	if (!count) return;
	int reserved = vt_preferences_subtitle_min_rows();
	if (reserved < count) reserved = count;
	int line_height = (int)size + (int)roundf(7 * scale);
	int anchor;
	switch (vt_preferences_subtitle_position()) {
		case VT_SUBTITLE_POSITION_LOWER:
			anchor = (int)(vh - 57 * scale);
			break;
		case VT_SUBTITLE_POSITION_LOW:
			anchor = (int)(vh - 92 * scale);
			break;
		case VT_SUBTITLE_POSITION_CENTER:
			anchor = (int)vh / 2 + (count - 1) * line_height / 2;
			break;
		case VT_SUBTITLE_POSITION_TOP:
			anchor = (int)(28 * scale) + (count - 1) * line_height;
			break;
		default:
			anchor = (int)(vh - 22 * scale);
			break;
	}
	/* Keep even XL top captions and reserved background rows inside the frame. */
	int minimum_anchor = reserved * line_height;
	if (anchor < minimum_anchor) anchor = minimum_anchor;
	anchor += (int)vy;
	int y = anchor - (count - 1) * line_height;
	int outline = (int)ceilf(vt_preferences_subtitle_outline_thickness() * scale);
	unsigned border = subtitle_border_color();
	unsigned foreground = subtitle_text_color();
	unsigned background = subtitle_background_color();
	char fitted[4][256];
	int widths[4] = {0};
	int widest = 0;
	for (int line = 0; line < count; line++) {
		ui_font_fit_text(font, size, lines[line], fitted[line],
		                 sizeof(fitted[line]), max_width);
		widths[line] = ui_font_text_width(font, size, fitted[line]);
		if (widths[line] > widest) widest = widths[line];
	}
	if (background) {
		int block_top = anchor - reserved * line_height + (int)(7 * scale);
		vita2d_draw_rectangle(vx + (vw - widest) / 2 - 12 * scale, block_top,
		                      widest + 24 * scale, reserved * line_height + 5 * scale,
		                      background);
	}
#ifdef VMD_ENABLE_PAF_NATIVE
	ui_native_vita_media_text(1);
#endif
	for (int line = 0; line < count; line++, y += line_height) {
		int width = widths[line];
		int x = (int)(vx + (vw - width) / 2);
		for (int radius = 1; radius <= outline; radius++) {
			ui_font_draw_text(font, x - radius, y, border, size, fitted[line]);
			ui_font_draw_text(font, x + radius, y, border, size, fitted[line]);
			ui_font_draw_text(font, x, y - radius, border, size, fitted[line]);
			ui_font_draw_text(font, x, y + radius, border, size, fitted[line]);
			ui_font_draw_text(font, x - radius, y - radius, border, size, fitted[line]);
			ui_font_draw_text(font, x + radius, y - radius, border, size, fitted[line]);
			ui_font_draw_text(font, x - radius, y + radius, border, size, fitted[line]);
			ui_font_draw_text(font, x + radius, y + radius, border, size, fitted[line]);
		}
		ui_font_draw_text(font, x, y, foreground, size, fitted[line]);
	}
#ifdef VMD_ENABLE_PAF_NATIVE
	ui_native_vita_media_text(0);
#endif
}
