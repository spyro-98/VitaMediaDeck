#include "ui/settings_screen.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "app_paths.h"
#include "i18n/i18n.h"
#include "network/network_source.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/focus_glow.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_reader.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define SCREEN_WIDTH 960
#define TAB_X 32
#define TAB_Y 67
#define TAB_W 171
#define TAB_H 40
#define ROW_X 44
#define ROW_Y 130
#define ROW_W 872
#define ROW_H 56
#define ROW_STEP 64
#define SETTINGS_VISIBLE_ROWS 5
#define SUBTITLE_VISIBLE_ROWS 3
#define SUBTITLE_ROW_W 520
#define SUBTITLE_PREVIEW_X 0
#define SUBTITLE_PREVIEW_Y 314
#define SUBTITLE_PREVIEW_W 960
#define SUBTITLE_PREVIEW_H 230

enum {
	TAB_PLAYBACK = 0,
	TAB_SUBTITLES,
	TAB_INTERFACE,
	TAB_SYSTEM,
	TAB_CONTROLS,
	TAB_COUNT
};

typedef struct {
	const char *label;
	int value;
	int kind;
} SettingRow;

enum {
	ROW_TOGGLE,
	ROW_LANGUAGE,
	ROW_CLOCK,
	ROW_MAPPING,
	ROW_DECODER,
	ROW_SUBTITLE_FONT,
	ROW_SUBTITLE_FOREGROUND,
	ROW_SUBTITLE_BACKGROUND,
	ROW_SUBTITLE_SIZE,
	ROW_SUBTITLE_WIDTH,
	ROW_SUBTITLE_ROWS,
	ROW_SUBTITLE_POSITION,
	ROW_ACTION
};

static const char *tab_label(int tab) {
	static const VtStringId labels[TAB_COUNT] = {
		VT_STR_SETTINGS_TAB_PLAYBACK,
		VT_STR_SETTINGS_TAB_SUBTITLES,
		VT_STR_SETTINGS_TAB_INTERFACE,
		VT_STR_SETTINGS_TAB_SYSTEM,
		VT_STR_SETTINGS_TAB_CONTROLS
	};
	return tab >= 0 && tab < TAB_COUNT ? vt_i18n_str(labels[tab]) : "";
}

static const char *subtitle_font_label(int font) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_FONT_MEDIUM,
		VT_STR_SETTINGS_SUBTITLE_FONT_SEMIBOLD,
		VT_STR_SETTINGS_SUBTITLE_FONT_SYSTEM
	};
	return vt_i18n_str(font >= VT_SUBTITLE_FONT_INTER_MEDIUM &&
	                   font <= VT_SUBTITLE_FONT_VITA_SYSTEM
	                   ? labels[font] : labels[0]);
}

static const char *subtitle_foreground_label(int color) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_COLOR_WHITE,
		VT_STR_SETTINGS_SUBTITLE_COLOR_YELLOW,
		VT_STR_SETTINGS_SUBTITLE_COLOR_CYAN,
		VT_STR_SETTINGS_SUBTITLE_COLOR_GREEN
	};
	return vt_i18n_str(color >= VT_SUBTITLE_TEXT_WHITE &&
	                   color <= VT_SUBTITLE_TEXT_GREEN
	                   ? labels[color] : labels[0]);
}

static const char *subtitle_background_label(int color) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_BG_TRANSPARENT,
		VT_STR_SETTINGS_SUBTITLE_BG_BLACK,
		VT_STR_SETTINGS_SUBTITLE_BG_MIDNIGHT,
		VT_STR_SETTINGS_SUBTITLE_BG_WHITE
	};
	return vt_i18n_str(color >= VT_SUBTITLE_BACKGROUND_TRANSPARENT &&
	                   color <= VT_SUBTITLE_BACKGROUND_WHITE
	                   ? labels[color] : labels[0]);
}

static const char *subtitle_size_label(int size) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_SIZE_SMALL,
		VT_STR_SETTINGS_SUBTITLE_SIZE_MEDIUM,
		VT_STR_SETTINGS_SUBTITLE_SIZE_LARGE
	};
	return vt_i18n_str(size >= VT_SUBTITLE_SIZE_SMALL &&
	                   size <= VT_SUBTITLE_SIZE_LARGE
	                   ? labels[size] : labels[VT_SUBTITLE_SIZE_MEDIUM]);
}

static const char *subtitle_position_label(int position) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_POSITION_BOTTOM,
		VT_STR_SETTINGS_SUBTITLE_POSITION_LOW,
		VT_STR_SETTINGS_SUBTITLE_POSITION_CENTER,
		VT_STR_SETTINGS_SUBTITLE_POSITION_HIGH
	};
	return vt_i18n_str(position >= VT_SUBTITLE_POSITION_BOTTOM &&
	                   position <= VT_SUBTITLE_POSITION_HIGH
	                   ? labels[position] : labels[0]);
}

static void draw_toggle(float x, float y, int enabled) {
	vita2d_draw_rectangle(x, y, 54, 28,
	                      enabled ? VT_THEME_BLUE : VT_THEME_BORDER);
	vita2d_draw_fill_circle(x + (enabled ? 40.0f : 14.0f), y + 14.0f,
	                        10.0f, VT_THEME_TEXT);
}

static void draw_value(vita2d_font *small, int y, int row_width,
	                   const char *value) {
	if (!small || !value) return;
	float width = ui_font_text_width(small, UI_FONT_SMALL, value);
	ui_font_draw_text(small, ROW_X + row_width - width - 22, y + 34,
	                  VT_THEME_BLUE_LIGHT, UI_FONT_SMALL, value);
}

/* The redesigned local/network UI currently ships complete English and
 * Italian catalogues. Keep the selector honest until the remaining language
 * catalogues have been translated against the new screens. */
static int language_count(void) { return 3; }

static const char *language_label(int language) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_LANGUAGE_SYSTEM,
		VT_STR_SETTINGS_LANGUAGE_ENGLISH,
		VT_STR_SETTINGS_LANGUAGE_ITALIAN
	};
	return vt_i18n_str(language >= 0 && language < language_count()
	                   ? labels[language] : labels[0]);
}

static const char *decoder_label(int decoder) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_DECODER_AUTO,
		VT_STR_SETTINGS_DECODER_HW_H264,
		VT_STR_SETTINGS_DECODER_SW_FFMPEG
	};
	return vt_i18n_str(decoder >= VT_VIDEO_DECODER_AUTO &&
	                   decoder <= VT_VIDEO_DECODER_SW_FFMPEG
	                   ? labels[decoder] : labels[VT_VIDEO_DECODER_AUTO]);
}

static int rows_for_tab(int tab, SettingRow rows[8]) {
	if (tab == TAB_PLAYBACK) {
		rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_VIDEO_DECODER), vt_preferences_video_decoder(), ROW_DECODER };
		rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_FILL_SCREEN), vt_preferences_fill_screen(), ROW_TOGGLE };
		rows[2] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_LOOP_PLAYBACK), vt_preferences_loop_enabled(), ROW_TOGGLE };
		rows[3] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_KEEP_MUSIC_AWAKE), vt_preferences_music_keep_display_awake(), ROW_TOGGLE };
		return 4;
	}
	if (tab == TAB_SUBTITLES) {
		rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_FONT), vt_preferences_subtitle_font(), ROW_SUBTITLE_FONT };
		rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_FOREGROUND), vt_preferences_subtitle_text_color(), ROW_SUBTITLE_FOREGROUND };
		rows[2] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_BACKGROUND), vt_preferences_subtitle_background_color(), ROW_SUBTITLE_BACKGROUND };
		rows[3] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_SIZE), vt_preferences_subtitle_size(), ROW_SUBTITLE_SIZE };
		rows[4] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_WIDTH), vt_preferences_subtitle_max_width(), ROW_SUBTITLE_WIDTH };
		rows[5] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_MIN_ROWS), vt_preferences_subtitle_min_rows(), ROW_SUBTITLE_ROWS };
		rows[6] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_MAX_ROWS), vt_preferences_subtitle_max_rows(), ROW_SUBTITLE_ROWS };
		rows[7] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_POSITION), vt_preferences_subtitle_position(), ROW_SUBTITLE_POSITION };
		return 8;
	}
	if (tab == TAB_INTERFACE) {
		rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_LANGUAGE), vt_preferences_language(), ROW_LANGUAGE };
		rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_REDUCE_MOTION), vt_preferences_reduce_motion(), ROW_TOGGLE };
		rows[2] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_ALWAYS_STATUS), vt_preferences_player_status_always_visible(), ROW_TOGGLE };
		return 3;
	}
	if (tab == TAB_SYSTEM) {
		rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_CLOCK_CONTROL), vt_preferences_clock_source(), ROW_CLOCK };
		rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_WRITE_LOGS), vt_preferences_disk_logs_enabled(), ROW_TOGGLE };
		rows[2] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_REMEMBER_NETWORK_PASSWORDS), vt_preferences_remember_network_passwords(), ROW_TOGGLE };
		return 3;
	}
	rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_CONTROL_MAPPING),
	                        vt_preferences_player_swap_shoulders(), ROW_MAPPING };
	rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_CONTROL_REFERENCE), 0, ROW_ACTION };
	return 2;
}

static int apply_row(int tab, int row, int direction) {
	if (tab == TAB_PLAYBACK) {
		switch (row) {
			case 0: {
				int decoder = vt_preferences_video_decoder() +
				              (direction < 0 ? -1 : 1);
				if (decoder < VT_VIDEO_DECODER_AUTO)
					decoder = VT_VIDEO_DECODER_SW_FFMPEG;
				if (decoder > VT_VIDEO_DECODER_SW_FFMPEG)
					decoder = VT_VIDEO_DECODER_AUTO;
				return vt_preferences_set_video_decoder(decoder);
			}
			case 1: return vt_preferences_set_fill_screen(!vt_preferences_fill_screen());
			case 2: return vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled());
			case 3: return vt_preferences_set_music_keep_display_awake(!vt_preferences_music_keep_display_awake());
		}
	} else if (tab == TAB_SUBTITLES) {
		switch (row) {
			case 0: {
				int value = vt_preferences_subtitle_font() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_FONT_INTER_MEDIUM)
					value = VT_SUBTITLE_FONT_VITA_SYSTEM;
				if (value > VT_SUBTITLE_FONT_VITA_SYSTEM)
					value = VT_SUBTITLE_FONT_INTER_MEDIUM;
				return vt_preferences_set_subtitle_font(value);
			}
			case 1: {
				int value = vt_preferences_subtitle_text_color() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_TEXT_WHITE) value = VT_SUBTITLE_TEXT_GREEN;
				if (value > VT_SUBTITLE_TEXT_GREEN) value = VT_SUBTITLE_TEXT_WHITE;
				return vt_preferences_set_subtitle_text_color(value);
			}
			case 2: {
				int value = vt_preferences_subtitle_background_color() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_BACKGROUND_TRANSPARENT)
					value = VT_SUBTITLE_BACKGROUND_WHITE;
				if (value > VT_SUBTITLE_BACKGROUND_WHITE)
					value = VT_SUBTITLE_BACKGROUND_TRANSPARENT;
				return vt_preferences_set_subtitle_background_color(value);
			}
			case 3: {
				int value = vt_preferences_subtitle_size() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_SIZE_SMALL) value = VT_SUBTITLE_SIZE_LARGE;
				if (value > VT_SUBTITLE_SIZE_LARGE) value = VT_SUBTITLE_SIZE_SMALL;
				return vt_preferences_set_subtitle_size(value);
			}
			case 4: {
				int value = vt_preferences_subtitle_max_width() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_WIDTH_60) value = VT_SUBTITLE_WIDTH_96;
				if (value > VT_SUBTITLE_WIDTH_96) value = VT_SUBTITLE_WIDTH_60;
				return vt_preferences_set_subtitle_max_width(value);
			}
			case 5: {
				int value = vt_preferences_subtitle_min_rows() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_MIN_ROWS) value = VT_SUBTITLE_MAX_ROWS;
				if (value > VT_SUBTITLE_MAX_ROWS) value = VT_SUBTITLE_MIN_ROWS;
				return vt_preferences_set_subtitle_min_rows(value);
			}
			case 6: {
				int value = vt_preferences_subtitle_max_rows() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_MIN_ROWS) value = VT_SUBTITLE_MAX_ROWS;
				if (value > VT_SUBTITLE_MAX_ROWS) value = VT_SUBTITLE_MIN_ROWS;
				return vt_preferences_set_subtitle_max_rows(value);
			}
			case 7: {
				int value = vt_preferences_subtitle_position() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_POSITION_BOTTOM)
					value = VT_SUBTITLE_POSITION_HIGH;
				if (value > VT_SUBTITLE_POSITION_HIGH)
					value = VT_SUBTITLE_POSITION_BOTTOM;
				return vt_preferences_set_subtitle_position(value);
			}
		}
	} else if (tab == TAB_INTERFACE) {
		if (row == 0) {
			int language = vt_preferences_language() + (direction < 0 ? -1 : 1);
			if (language < 0) language = language_count() - 1;
			if (language >= language_count()) language = 0;
			return vt_i18n_set_language(language);
		} else if (row == 1) {
			return vt_preferences_set_reduce_motion(!vt_preferences_reduce_motion());
		} else if (row == 2) {
			return vt_preferences_set_player_status_always_visible(
			       !vt_preferences_player_status_always_visible());
		}
	} else if (tab == TAB_SYSTEM) {
		if (row == 0) {
			int value = vt_preferences_clock_source() == VT_CLOCK_SOURCE_APP
			          ? VT_CLOCK_SOURCE_PSVSHELL : VT_CLOCK_SOURCE_APP;
			return vt_preferences_set_clock_source(value);
		} else if (row == 1) {
			return vt_preferences_set_disk_logs_enabled(!vt_preferences_disk_logs_enabled());
		} else if (row == 2) {
			int enabled = !vt_preferences_remember_network_passwords();
			if (!enabled) {
				int clear_result = vt_network_credentials_clear();
				if (clear_result < 0) return clear_result;
			}
			return vt_preferences_set_remember_network_passwords(enabled);
		}
	} else if (tab == TAB_CONTROLS && row == 0) {
		return vt_preferences_set_player_swap_shoulders(
		       !vt_preferences_player_swap_shoulders());
	}
	return 0;
}

static int visible_rows_for_tab(int tab) {
	return tab == TAB_SUBTITLES ? SUBTITLE_VISIBLE_ROWS : SETTINGS_VISIBLE_ROWS;
}

static int first_visible_row(int cursor, int row_count, int visible_rows) {
	if (row_count <= visible_rows) return 0;
	int first = cursor - visible_rows / 2;
	if (first < 0) first = 0;
	int maximum = row_count - visible_rows;
	if (first > maximum) first = maximum;
	return first;
}

static unsigned subtitle_preview_text_color(void) {
	switch (vt_preferences_subtitle_text_color()) {
		case VT_SUBTITLE_TEXT_YELLOW: return RGBA8(255, 232, 96, 255);
		case VT_SUBTITLE_TEXT_CYAN: return RGBA8(108, 235, 255, 255);
		case VT_SUBTITLE_TEXT_GREEN: return RGBA8(124, 244, 148, 255);
		default: return RGBA8(255, 255, 255, 255);
	}
}

static unsigned subtitle_preview_border_color(void) {
	switch (vt_preferences_subtitle_border_color()) {
		case VT_SUBTITLE_BORDER_MIDNIGHT: return RGBA8(3, 18, 34, 255);
		case VT_SUBTITLE_BORDER_WHITE: return RGBA8(255, 255, 255, 255);
		case VT_SUBTITLE_BORDER_YELLOW: return RGBA8(255, 210, 48, 255);
		default: return RGBA8(0, 0, 0, 255);
	}
}

static unsigned subtitle_preview_background_color(void) {
	switch (vt_preferences_subtitle_background_color()) {
		case VT_SUBTITLE_BACKGROUND_BLACK: return RGBA8(0, 0, 0, 198);
		case VT_SUBTITLE_BACKGROUND_MIDNIGHT: return RGBA8(3, 18, 34, 210);
		case VT_SUBTITLE_BACKGROUND_WHITE: return RGBA8(255, 255, 255, 205);
		default: return 0;
	}
}

static int subtitle_preview_width_percent(void) {
	static const int widths[] = { 60, 75, 88, 96 };
	int value = vt_preferences_subtitle_max_width();
	return value >= VT_SUBTITLE_WIDTH_60 && value <= VT_SUBTITLE_WIDTH_96
	     ? widths[value] : widths[VT_SUBTITLE_WIDTH_88];
}

static unsigned subtitle_preview_font_size(void) {
	return vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_SMALL
	     ? UI_FONT_SMALL
	     : vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_LARGE
	     ? UI_FONT_DISPLAY : UI_FONT_BODY;
}

static void draw_subtitle_preview(vita2d_font *label_font) {
	static const char *const samples[] = {
		"The network is quiet tonight.",
		"That does not mean nobody is listening.",
		"Keep moving. We are almost there.",
		"I will contact you on the other side."
	};
	const int viewport_x = SUBTITLE_PREVIEW_X;
	const int viewport_y = SUBTITLE_PREVIEW_Y + 24;
	const int viewport_w = SUBTITLE_PREVIEW_W;
	const int viewport_h = SUBTITLE_PREVIEW_H - 24;
	ui_panel(SUBTITLE_PREVIEW_X, SUBTITLE_PREVIEW_Y,
	         SUBTITLE_PREVIEW_W, SUBTITLE_PREVIEW_H,
	         VT_THEME_SURFACE, VT_THEME_COLD_DIM, 0);
	vita2d_draw_rectangle(0, SUBTITLE_PREVIEW_Y, 960, 2,
	                      VT_THEME_SPECTRAL_A(80));
	if (label_font)
		ui_font_draw_text(label_font, 44,
		                  SUBTITLE_PREVIEW_Y + 19, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_PREVIEW));

	/* A full-width mock movie frame exercises the exact screen-relative caption
	 * width and position instead of presenting text inside a narrow settings card. */
	vita2d_draw_rectangle(viewport_x, viewport_y, viewport_w, viewport_h,
	                      VT_THEME_BG);
	/* Abstract night skyline and reflected data haze: enough contrast variation
	 * to judge outline/background settings like a real scene. */
	for (int building = 0; building < 13; building++) {
		int width = 54 + (building * 17) % 48;
		int height = 34 + (building * 29) % 94;
		int x = building * 79 - 18;
		vita2d_draw_rectangle(x, viewport_y + viewport_h - height,
		                      width, height,
		                      building & 1 ? RGBA8(4, 17, 24, 255)
		                                   : RGBA8(7, 25, 34, 255));
		if (building % 3 == 0)
			vita2d_draw_rectangle(x + 11, viewport_y + viewport_h - height + 13,
			                      4, 2, VT_THEME_SIGNAL_A(118));
	}
	vita2d_draw_line(0, viewport_y + viewport_h - 48,
	                 960, viewport_y + viewport_h - 72, VT_THEME_COLD_A(76));
	for (int particle = 0; particle < 42; particle++) {
		unsigned seed = 0x45D9F3BU * (unsigned)(particle + 31);
		seed ^= seed >> 15;
		float px = (float)(seed % 960U);
		float py = viewport_y + 8.0f + (float)((seed >> 10) % (unsigned)(viewport_h - 42));
		vita2d_draw_fill_circle(px, py, particle % 9 == 0 ? 2.0f : 1.0f,
		                        particle % 11 == 0 ? VT_THEME_SIGNAL_A(105)
		                                            : VT_THEME_COLD_A(92));
	}

	unsigned size = subtitle_preview_font_size();
	vita2d_font *font = ui_runtime_subtitle_font(
	    vt_preferences_subtitle_font(), size);
	int max_width = viewport_w * subtitle_preview_width_percent() / 100;
	int guide_left = viewport_x + (viewport_w - max_width) / 2;
	int guide_right = guide_left + max_width;
	vita2d_draw_line(guide_left, viewport_y + 8, guide_left,
	                 viewport_y + viewport_h - 10, VT_THEME_COLD_DIM);
	vita2d_draw_line(guide_right, viewport_y + 8, guide_right,
	                 viewport_y + viewport_h - 10, VT_THEME_COLD_DIM);

	int count = vt_preferences_subtitle_max_rows();
	if (count < 1) count = 1;
	if (count > 4) count = 4;
	int reserved = vt_preferences_subtitle_min_rows();
	if (reserved < count) reserved = count;
	if (reserved > 4) reserved = 4;
	int line_height = (int)size + 7;
	int anchor;
	switch (vt_preferences_subtitle_position()) {
		case VT_SUBTITLE_POSITION_LOW: anchor = viewport_y + viewport_h - 50; break;
		case VT_SUBTITLE_POSITION_CENTER: anchor = viewport_y + viewport_h / 2 + 18; break;
		case VT_SUBTITLE_POSITION_HIGH: anchor = viewport_y + 54; break;
		default: anchor = viewport_y + viewport_h - 15; break;
	}
	char fitted[4][160];
	int widths[4] = {0};
	int widest = 0;
	for (int line = 0; line < count; line++) {
		ui_font_fit_text(font, size, samples[line], fitted[line],
		                 sizeof(fitted[line]), max_width);
		widths[line] = ui_font_text_width(font, size, fitted[line]);
		if (widths[line] > widest) widest = widths[line];
	}
	unsigned background = subtitle_preview_background_color();
	if (background) {
		int block_top = anchor - reserved * line_height + 7;
		vita2d_draw_rectangle(viewport_x + (viewport_w - widest) / 2 - 7,
		                      block_top, widest + 14,
		                      reserved * line_height + 5, background);
	}
	unsigned foreground = subtitle_preview_text_color();
	unsigned border = subtitle_preview_border_color();
	int outline = vt_preferences_subtitle_outline_thickness();
	int y = anchor - (count - 1) * line_height;
	for (int line = 0; line < count; line++, y += line_height) {
		int x = viewport_x + (viewport_w - widths[line]) / 2;
		for (int radius = 1; radius <= outline; radius++) {
			ui_font_draw_text(font, x - radius, y, border, size, fitted[line]);
			ui_font_draw_text(font, x + radius, y, border, size, fitted[line]);
			ui_font_draw_text(font, x, y - radius, border, size, fitted[line]);
			ui_font_draw_text(font, x, y + radius, border, size, fitted[line]);
		}
		ui_font_draw_text(font, x, y, foreground, size, fitted[line]);
	}

}

void ui_settings_show_controls_reference(void) {
	ui_text_reader_run_tabbed(vt_i18n_str(VT_STR_SETTINGS_TITLE),
	                          vt_i18n_str(VT_STR_SETTINGS_TAB_CONTROLS),
	                          vt_i18n_str(VT_STR_SETTINGS_CONTROLS_REFERENCE_IT));
}

static void draw_screen(int tab, int cursor, float focus,
	                    UiSectionsSidebar *sidebar) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	SettingRow rows[8];
	int row_count = rows_for_tab(tab, rows);
	int row_limit = visible_rows_for_tab(tab);
	int first_row = first_visible_row(cursor, row_count, row_limit);
	int visible_rows = row_count - first_row;
	if (visible_rows > row_limit) visible_rows = row_limit;
	int page_has_focus = !ui_mini_player_input_locked() && !sidebar->open &&
	                     sidebar->animation <= 0.01f;
	int row_width = tab == TAB_SUBTITLES ? SUBTITLE_ROW_W : ROW_W;
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header(NULL);

	for (int i = 0; i < TAB_COUNT; i++) {
		int x = TAB_X + i * (TAB_W + 8);
		ui_panel(x, TAB_Y, TAB_W, TAB_H,
		         i == tab ? VT_THEME_SURFACE_RAISED : VT_THEME_SURFACE,
		         i == tab ? VT_THEME_SIGNAL_LIGHT : VT_THEME_BORDER_DIM,
		         0);
		if (small) {
			char index[4];
			snprintf(index, sizeof(index), "%02d", i + 1);
			ui_font_draw_text(small, x + 11, TAB_Y + 26,
			                  i == tab ? VT_THEME_SIGNAL_LIGHT : VT_THEME_TEXT_FAINT,
			                  UI_FONT_SMALL, index);
		}
		if (body) ui_font_draw_text(body, x + 43, TAB_Y + 27,
		                             i == tab ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
		                             UI_FONT_BODY, tab_label(i));
		if (i == tab) {
			vita2d_draw_rectangle(x + 8, TAB_Y + TAB_H - 3, TAB_W - 16, 3,
			                      VT_THEME_SIGNAL_LIGHT);
			vita2d_draw_rectangle(x + 8, TAB_Y + 4, 28, 1,
			                      VT_THEME_COLD_LIGHT);
		}
	}
	vita2d_draw_rectangle(TAB_X, TAB_Y + TAB_H + 8,
	                      TAB_COUNT * TAB_W + (TAB_COUNT - 1) * 8, 1,
	                      VT_THEME_BORDER_DIM);
	vita2d_draw_rectangle(TAB_X, TAB_Y + TAB_H + 8,
	                      (tab + 1) * TAB_W + tab * 8, 2,
	                      VT_THEME_SPECTRAL);
	if (page_has_focus)
		ui_focus_glow_draw(ROW_X, ROW_Y + focus * ROW_STEP, row_width, ROW_H,
			                   sceKernelGetProcessTimeWide(), ROW_Y,
			                   tab == TAB_SUBTITLES ? SUBTITLE_PREVIEW_Y - 8 : 432);

	for (int slot = 0; slot < visible_rows; slot++) {
		int i = first_row + slot;
		int y = ROW_Y + slot * ROW_STEP;
		ui_panel(ROW_X, y, row_width, ROW_H, VT_THEME_SURFACE,
		         page_has_focus && i == cursor
		             ? VT_THEME_SIGNAL_LIGHT : VT_THEME_BORDER_DIM, 0);
		vita2d_draw_rectangle(ROW_X + 12, y + 14, 3, ROW_H - 28,
		                      page_has_focus && i == cursor
		                          ? VT_THEME_SIGNAL : VT_THEME_BORDER_DIM);
		if (body) ui_font_draw_text(body, ROW_X + 22, y + 35,
		                             page_has_focus && i == cursor
		                                 ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
		                             UI_FONT_BODY, rows[i].label);
		if (rows[i].kind == ROW_TOGGLE)
			draw_toggle(ROW_X + row_width - 78, y + 14, rows[i].value);
		else if (rows[i].kind == ROW_LANGUAGE)
			draw_value(small, y, row_width, language_label(rows[i].value));
		else if (rows[i].kind == ROW_CLOCK)
			draw_value(small, y, row_width, rows[i].value == VT_CLOCK_SOURCE_APP
			                       ? "VitaMediaDeck 444/222/222/111"
			                       : vt_i18n_str(VT_STR_SETTINGS_CLOCK_SOURCE_PSVSHELL));
		else if (rows[i].kind == ROW_MAPPING)
			draw_value(small, y, row_width, vt_i18n_str(rows[i].value
			    ? VT_STR_SETTINGS_CONTROL_DPAD_PANELS
			    : VT_STR_SETTINGS_CONTROL_SHOULDERS_PANELS));
		else if (rows[i].kind == ROW_DECODER)
			draw_value(small, y, row_width, decoder_label(rows[i].value));
		else if (rows[i].kind == ROW_SUBTITLE_FONT)
			draw_value(small, y, row_width, subtitle_font_label(rows[i].value));
		else if (rows[i].kind == ROW_SUBTITLE_FOREGROUND)
			draw_value(small, y, row_width, subtitle_foreground_label(rows[i].value));
		else if (rows[i].kind == ROW_SUBTITLE_BACKGROUND)
			draw_value(small, y, row_width, subtitle_background_label(rows[i].value));
		else if (rows[i].kind == ROW_SUBTITLE_SIZE)
			draw_value(small, y, row_width, subtitle_size_label(rows[i].value));
		else if (rows[i].kind == ROW_SUBTITLE_WIDTH) {
			static const int widths[] = { 60, 75, 88, 96 };
			char value[16];
			int index = rows[i].value >= VT_SUBTITLE_WIDTH_60 &&
			            rows[i].value <= VT_SUBTITLE_WIDTH_96
			          ? rows[i].value : VT_SUBTITLE_WIDTH_88;
			snprintf(value, sizeof(value), "%d%%", widths[index]);
			draw_value(small, y, row_width, value);
		} else if (rows[i].kind == ROW_SUBTITLE_ROWS) {
			char value[16];
			snprintf(value, sizeof(value),
			         vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_ROWS_FORMAT),
			         rows[i].value);
			draw_value(small, y, row_width, value);
		} else if (rows[i].kind == ROW_SUBTITLE_POSITION)
			draw_value(small, y, row_width, subtitle_position_label(rows[i].value));
		else draw_value(small, y, row_width, vt_i18n_str(VT_STR_SETTINGS_OPEN));
	}
	if (row_count > row_limit && small) {
		if (first_row > 0)
			ui_font_draw_text(small, ROW_X + row_width - 18, ROW_Y - 7,
			                  VT_THEME_BLUE_LIGHT, UI_FONT_SMALL, "^");
		if (first_row + visible_rows < row_count)
			ui_font_draw_text(small, ROW_X + row_width - 18,
			                  tab == TAB_SUBTITLES ? SUBTITLE_PREVIEW_Y - 9 : 453,
			                  VT_THEME_BLUE_LIGHT, UI_FONT_SMALL, "v");
	}
	if (tab == TAB_SYSTEM && small) {
		char path[192];
		snprintf(path, sizeof(path), vt_i18n_str(VT_STR_SETTINGS_PASSWORD_PATH),
		         VITAMEDIADECK_NETWORK_PASSWORDS_PATH);
		ui_font_draw_text(small, ROW_X + 12, 354, VT_THEME_WARNING,
		                  UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_SETTINGS_PASSWORD_PLAINTEXT_WARNING));
		ui_font_draw_text(small, ROW_X + 12, 382, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, path);
	}
	if (tab == TAB_SUBTITLES) {
		draw_subtitle_preview(small);
	} else if (small) {
		const char *footer = vt_i18n_str(VT_STR_SETTINGS_FOOTER_HINT);
		char fitted[512];
		ui_font_fit_text(small, UI_FONT_SMALL, footer, fitted, sizeof(fitted),
		                 ROW_W);
		ui_font_draw_text(small, ROW_X, 462, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, fitted);
	}

	ui_mini_player_draw();
	if (sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_settings_screen(void) {
	if (!ui_runtime_is_ready()) return UI_SECTION_NONE;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_SETTINGS);
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	int tab = 0, cursor = 0;
	float focus = 0.0f;
	uint64_t focus_tick_us = 0;
	UiNavRepeat nav_repeat;
	ui_nav_repeat_reset(&nav_repeat);
	for (;;) {
		ui_mini_player_pump();
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
		int was_open = sidebar.open;
		int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
		                                                 ctrl.buttons, ctrl.ly);
		int sidebar_owned_frame = sidebar.open || was_open;
		if (sidebar_owned_frame) {
			int touched = ui_sections_sidebar_handle_touch(&sidebar, touch_flags,
			                                                  touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
			touch_flags = UI_TOUCH_EVENT_NONE;
		} else if (sidebar.animation <= 0.01f &&
		           ui_mini_player_handle_touch(touch_flags, &touch))
			touch_flags = UI_TOUCH_EVENT_NONE;
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) return section;
		if (!sidebar.open && sidebar.animation > 0.01f) {
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		int page_owns_input = !sidebar_owned_frame && !sidebar.open &&
		                      sidebar.animation <= 0.01f;
		if (page_owns_input && (pressed & SCE_CTRL_CIRCLE)) return UI_SECTION_NONE;

		if (page_owns_input) {
			unsigned int nav = ui_nav_repeat_update(
			    &nav_repeat, pressed, ctrl.buttons, ctrl.lx, ctrl.ly,
			    SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
			if (nav & SCE_CTRL_LEFT) {
				tab = tab > 0 ? tab - 1 : TAB_COUNT - 1;
				cursor = 0;
			}
			if (nav & SCE_CTRL_RIGHT) {
				tab = (tab + 1) % TAB_COUNT;
				cursor = 0;
			}
			SettingRow rows[8];
			int count = rows_for_tab(tab, rows);
			if ((nav & SCE_CTRL_UP) && cursor > 0) cursor--;
			if ((nav & SCE_CTRL_DOWN) && cursor + 1 < count) cursor++;
			if (pressed & SCE_CTRL_CROSS) {
				if (tab == TAB_CONTROLS && cursor == 1) {
					ui_settings_show_controls_reference();
					ui_touch_reset();
					sceCtrlPeekBufferPositive(0, &previous, 1);
				} else if (apply_row(tab, cursor, 1) < 0) {
					ui_message_show(vt_i18n_str(VT_STR_SETTINGS_SAVE_FAILED_TITLE),
					                vt_i18n_str(VT_STR_SETTINGS_SAVE_FAILED_DETAIL), 2600);
				}
			}
			if ((pressed & SCE_CTRL_LTRIGGER) == 0 &&
			    (pressed & SCE_CTRL_RTRIGGER)) {
				tab = (tab + 1) % TAB_COUNT;
				cursor = 0;
			}
			if (touch_flags & UI_TOUCH_EVENT_TAP) {
				for (int i = 0; i < TAB_COUNT; i++) {
					int x = TAB_X + i * (TAB_W + 8);
					if (ui_touch_hit_rect(touch.x, touch.y, x, TAB_Y, TAB_W, TAB_H)) {
						tab = i;
						cursor = 0;
					}
				}
				count = rows_for_tab(tab, rows);
				int row_limit = visible_rows_for_tab(tab);
				int first_row = first_visible_row(cursor, count, row_limit);
				int visible_rows = count - first_row;
				if (visible_rows > row_limit) visible_rows = row_limit;
				for (int slot = 0; slot < visible_rows; slot++) {
					int i = first_row + slot;
					int row_width = tab == TAB_SUBTITLES
					              ? SUBTITLE_ROW_W : ROW_W;
					if (ui_touch_hit_rect(touch.x, touch.y, ROW_X,
					                      ROW_Y + slot * ROW_STEP, row_width, ROW_H)) {
						cursor = i;
						if (tab == TAB_CONTROLS && cursor == 1)
							ui_settings_show_controls_reference();
						else if (apply_row(tab, cursor, 1) < 0)
							ui_message_show(vt_i18n_str(VT_STR_SETTINGS_SAVE_FAILED_TITLE),
							                vt_i18n_str(VT_STR_SETTINGS_SAVE_FAILED_DETAIL), 2600);
						ui_touch_reset();
						sceCtrlPeekBufferPositive(0, &previous, 1);
						break;
					}
				}
			}
		} else ui_nav_repeat_reset(&nav_repeat);
		SettingRow focus_rows[8];
		int focus_count = rows_for_tab(tab, focus_rows);
		int focus_first = first_visible_row(cursor, focus_count,
		                                    visible_rows_for_tab(tab));
		float target = (float)(cursor - focus_first);
		if (vt_preferences_reduce_motion()) focus = target;
		else {
			uint64_t now = sceKernelGetProcessTimeWide();
			uint64_t elapsed = focus_tick_us && now > focus_tick_us
			                 ? now - focus_tick_us : 16667ULL;
			if (elapsed > 50000ULL) elapsed = 50000ULL;
			focus_tick_us = now;
			float alpha = (float)elapsed / (55000.0f + (float)elapsed);
			focus += (target - focus) * alpha;
		}
		draw_screen(tab, cursor, focus, &sidebar);
		sceKernelDelayThread(vt_preferences_reduce_motion() ? 16000 : 1000);
	}
}
