#include "ui/list_scroll.h"
#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
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
#include "ui/layout.h"
#include "ui/home_tabs.h"
#include "ui/components.h"
#include "ui/focus_glow.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/subtitle.h"
#include "ui/sections_sidebar.h"
#include "ui/text_reader.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define SCREEN_WIDTH 960
#define TAB_X UI_CONTENT_X
#define TAB_Y UI_TABS_Y
#define TAB_W 178
#define TAB_H 40
#define ROW_X UI_CONTENT_X
#define ROW_Y 118
#define ROW_W UI_CONTENT_W
#define ROW_H 56
#define ROW_STEP 64
#define SETTINGS_VISIBLE_ROWS 5
#define SUBTITLE_VISIBLE_ROWS 5
#define SUBTITLE_ROW_W 386
#define SUBTITLE_PREVIEW_X 427
#define SUBTITLE_PREVIEW_Y 172
#define SUBTITLE_PREVIEW_W 515
#define SUBTITLE_PREVIEW_H 292

enum {
	TAB_SYSTEM = 0,
	TAB_INTERFACE,
	TAB_PLAYBACK,
	TAB_SUBTITLES,
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
	ROW_INTERFACE_THEME,
	ROW_CLOCK,
 ROW_CPU,
	ROW_MAPPING,
	ROW_DECODER,
	ROW_SUBTITLE_FONT,
	ROW_SUBTITLE_FOREGROUND,
	ROW_SUBTITLE_BORDER,
	ROW_SUBTITLE_BACKGROUND,
	ROW_SUBTITLE_SIZE,
	ROW_SUBTITLE_WIDTH,
	ROW_SUBTITLE_ROWS,
	ROW_SUBTITLE_POSITION,
	ROW_ACTION
};

static const char *tab_label(int tab) {
	static const VtStringId labels[TAB_COUNT] = {
		VT_STR_SETTINGS_TAB_SYSTEM,
		VT_STR_SETTINGS_TAB_INTERFACE,
		VT_STR_SETTINGS_TAB_PLAYBACK,
		VT_STR_SETTINGS_TAB_SUBTITLES,
		VT_STR_SETTINGS_TAB_CONTROLS
	};
	return tab >= 0 && tab < TAB_COUNT ? vt_i18n_str(labels[tab]) : "";
}

static const char *subtitle_font_label(int font) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_FONT_MEDIUM,
		VT_STR_SETTINGS_SUBTITLE_FONT_SEMIBOLD,
		VT_STR_SETTINGS_SUBTITLE_FONT_SYSTEM,
		VT_STR_SETTINGS_SUBTITLE_FONT_JAPANESE,
		VT_STR_SETTINGS_SUBTITLE_FONT_CHINESE,
		VT_STR_SETTINGS_SUBTITLE_FONT_KOREAN,
		VT_STR_SETTINGS_SUBTITLE_FONT_LATIN
	};
	return vt_i18n_str(font >= VT_SUBTITLE_FONT_INTER_MEDIUM &&
	                   font <= VT_SUBTITLE_FONT_VITA_LATIN
	                   ? labels[font] : labels[0]);
}

static const char *subtitle_foreground_label(int color) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_COLOR_WHITE,
		VT_STR_SETTINGS_SUBTITLE_COLOR_YELLOW,
		VT_STR_SETTINGS_SUBTITLE_COLOR_CYAN,
		VT_STR_SETTINGS_SUBTITLE_COLOR_GREEN,
		VT_STR_SETTINGS_SUBTITLE_COLOR_ORANGE,
		VT_STR_SETTINGS_SUBTITLE_COLOR_MAGENTA,
		VT_STR_SETTINGS_SUBTITLE_COLOR_BLUE,
		VT_STR_SETTINGS_SUBTITLE_COLOR_GRAY
	};
	return vt_i18n_str(color >= VT_SUBTITLE_TEXT_WHITE &&
	                   color <= VT_SUBTITLE_TEXT_GRAY
	                   ? labels[color] : labels[0]);
}

static const char *subtitle_border_label(int color) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_BG_BLACK,
		VT_STR_SETTINGS_SUBTITLE_BG_MIDNIGHT,
		VT_STR_SETTINGS_SUBTITLE_COLOR_WHITE,
		VT_STR_SETTINGS_SUBTITLE_COLOR_YELLOW,
		VT_STR_SETTINGS_SUBTITLE_COLOR_CYAN,
		VT_STR_SETTINGS_SUBTITLE_COLOR_ORANGE,
		VT_STR_SETTINGS_SUBTITLE_COLOR_BLUE,
		VT_STR_SETTINGS_SUBTITLE_COLOR_GRAY
	};
	return vt_i18n_str(color >= VT_SUBTITLE_BORDER_BLACK &&
	                   color <= VT_SUBTITLE_BORDER_GRAY
	                   ? labels[color] : labels[0]);
}

static const char *subtitle_background_label(int color) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_BG_TRANSPARENT,
		VT_STR_SETTINGS_SUBTITLE_BG_BLACK,
		VT_STR_SETTINGS_SUBTITLE_BG_MIDNIGHT,
		VT_STR_SETTINGS_SUBTITLE_BG_WHITE,
		VT_STR_SETTINGS_SUBTITLE_BG_NAVY,
		VT_STR_SETTINGS_SUBTITLE_BG_BURGUNDY,
		VT_STR_SETTINGS_SUBTITLE_BG_FOREST,
		VT_STR_SETTINGS_SUBTITLE_BG_SMOKE
	};
	return vt_i18n_str(color >= VT_SUBTITLE_BACKGROUND_TRANSPARENT &&
	                   color <= VT_SUBTITLE_BACKGROUND_SMOKE
	                   ? labels[color] : labels[0]);
}

static const char *subtitle_size_label(int size) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_SIZE_SMALL,
		VT_STR_SETTINGS_SUBTITLE_SIZE_MEDIUM,
		VT_STR_SETTINGS_SUBTITLE_SIZE_LARGE,
		VT_STR_SETTINGS_SUBTITLE_SIZE_EXTRA_LARGE,
		VT_STR_SETTINGS_SUBTITLE_SIZE_MEDIUM_LARGE
	};
	return vt_i18n_str(size >= VT_SUBTITLE_SIZE_SMALL &&
	                   size <= VT_SUBTITLE_SIZE_MEDIUM_LARGE
	                   ? labels[size] : labels[VT_SUBTITLE_SIZE_MEDIUM]);
}

static const char *subtitle_position_label(int position) {
	static const VtStringId labels[] = {
		VT_STR_SETTINGS_SUBTITLE_POSITION_BOTTOM,
		VT_STR_SETTINGS_SUBTITLE_POSITION_LOW,
		VT_STR_SETTINGS_SUBTITLE_POSITION_CENTER,
		VT_STR_SETTINGS_SUBTITLE_POSITION_TOP,
		VT_STR_SETTINGS_SUBTITLE_POSITION_LOWER
	};
	return vt_i18n_str(position >= VT_SUBTITLE_POSITION_BOTTOM &&
	                   position <= VT_SUBTITLE_POSITION_LOWER
	                   ? labels[position] : labels[0]);
}

static void draw_toggle(float x, float y, int enabled) {
#ifdef VMD_ENABLE_PAF_NATIVE
    if (ui_native_vita_toggle(x + 5, y - 8, 44, 44, enabled)) return;
#endif
	vita2d_draw_rectangle(x, y, 54, 28,
	                      enabled ? VT_THEME_BLUE : VT_THEME_BORDER);
	vita2d_draw_fill_circle(x + (enabled ? 40.0f : 14.0f), y + 14.0f,
	                        10.0f, UI_MENU_TEXT);
}

static void draw_value(vita2d_font *small, int y, int row_width,
	                   const char *value) {
	if (!small || !value) return;
	if (row_width == SUBTITLE_ROW_W) y += 12;
	char fitted[256];
	ui_font_fit_text(small,UI_FONT_SMALL,value,fitted,sizeof(fitted),
	    row_width==SUBTITLE_ROW_W ? row_width-32 : row_width/2-32);
	value=fitted;
	float width = ui_font_text_width(small, UI_FONT_SMALL, value);
	ui_font_draw_text_exact(small, ROW_X + row_width - width - 22, y + 34,
	                  UI_MENU_ACCENT, UI_FONT_SMALL, value);
}

static int language_count(void) { return VT_LANG_COUNT; }
static const char *language_label(int language) {return vt_i18n_language_name(language);}

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

static int rows_for_tab(int tab, SettingRow rows[9]) {
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
		rows[2] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_BORDER), vt_preferences_subtitle_border_color(), ROW_SUBTITLE_BORDER };
		rows[3] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_BACKGROUND), vt_preferences_subtitle_background_color(), ROW_SUBTITLE_BACKGROUND };
		rows[4] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_SIZE), vt_preferences_subtitle_size(), ROW_SUBTITLE_SIZE };
		rows[5] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_WIDTH), vt_preferences_subtitle_max_width(), ROW_SUBTITLE_WIDTH };
		rows[6] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_MIN_ROWS), vt_preferences_subtitle_min_rows(), ROW_SUBTITLE_ROWS };
		rows[7] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_MAX_ROWS), vt_preferences_subtitle_max_rows(), ROW_SUBTITLE_ROWS };
		rows[8] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_POSITION), vt_preferences_subtitle_position(), ROW_SUBTITLE_POSITION };
		return 9;
	}
	if (tab == TAB_INTERFACE) {
		rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_LANGUAGE), vt_preferences_language(), ROW_LANGUAGE };
		rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_REDUCE_MOTION), vt_preferences_reduce_motion(), ROW_TOGGLE };
		rows[2] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_ALWAYS_STATUS), vt_preferences_player_status_always_visible(), ROW_TOGGLE };
		rows[3] = (SettingRow){ vt_i18n_str(VT_STR_HOME_TAB_ORDER), 0, ROW_ACTION };
		rows[4] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_INTERFACE_THEME), vt_preferences_interface_theme(), ROW_INTERFACE_THEME };
		return 5;
	}
	if (tab == TAB_SYSTEM) {
		rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_CLOCK_CONTROL), vt_preferences_clock_source(), ROW_CLOCK };
		rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_WRITE_LOGS), vt_preferences_disk_logs_enabled(), ROW_TOGGLE };
		rows[2] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_REMEMBER_NETWORK_PASSWORDS), vt_preferences_remember_network_passwords(), ROW_TOGGLE };
        rows[3] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_HW_CPU), vt_preferences_decoder_cpu_mhz(1), ROW_CPU };
        rows[4] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_SW_CPU), vt_preferences_decoder_cpu_mhz(0), ROW_CPU };
		return 5;
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
					value = VT_SUBTITLE_FONT_VITA_LATIN;
				if (value > VT_SUBTITLE_FONT_VITA_LATIN)
					value = VT_SUBTITLE_FONT_INTER_MEDIUM;
				return vt_preferences_set_subtitle_font(value);
			}
			case 1: {
				int value = vt_preferences_subtitle_text_color() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_TEXT_WHITE) value = VT_SUBTITLE_TEXT_GRAY;
				if (value > VT_SUBTITLE_TEXT_GRAY) value = VT_SUBTITLE_TEXT_WHITE;
				return vt_preferences_set_subtitle_text_color(value);
			}
			case 2: {
				int value = vt_preferences_subtitle_border_color() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_BORDER_BLACK) value = VT_SUBTITLE_BORDER_GRAY;
				if (value > VT_SUBTITLE_BORDER_GRAY) value = VT_SUBTITLE_BORDER_BLACK;
				return vt_preferences_set_subtitle_border_color(value);
			}
			case 3: {
				int value = vt_preferences_subtitle_background_color() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_BACKGROUND_TRANSPARENT)
					value = VT_SUBTITLE_BACKGROUND_SMOKE;
				if (value > VT_SUBTITLE_BACKGROUND_SMOKE)
					value = VT_SUBTITLE_BACKGROUND_TRANSPARENT;
				return vt_preferences_set_subtitle_background_color(value);
			}
			case 4: {
				static const int order[] = {VT_SUBTITLE_SIZE_SMALL,
				    VT_SUBTITLE_SIZE_MEDIUM, VT_SUBTITLE_SIZE_MEDIUM_LARGE,
				    VT_SUBTITLE_SIZE_LARGE, VT_SUBTITLE_SIZE_EXTRA_LARGE};
				int index = 0;
				while (index < 4 && order[index] != vt_preferences_subtitle_size()) index++;
				return vt_preferences_set_subtitle_size(order[(index + (direction < 0 ? 4 : 1)) % 5]);
			}
			case 5: {
				int value = vt_preferences_subtitle_max_width() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_WIDTH_60) value = VT_SUBTITLE_WIDTH_96;
				if (value > VT_SUBTITLE_WIDTH_96) value = VT_SUBTITLE_WIDTH_60;
				return vt_preferences_set_subtitle_max_width(value);
			}
			case 6: {
				int value = vt_preferences_subtitle_min_rows() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_MIN_ROWS) value = VT_SUBTITLE_MAX_ROWS;
				if (value > VT_SUBTITLE_MAX_ROWS) value = VT_SUBTITLE_MIN_ROWS;
				return vt_preferences_set_subtitle_min_rows(value);
			}
			case 7: {
				int value = vt_preferences_subtitle_max_rows() +
				            (direction < 0 ? -1 : 1);
				if (value < VT_SUBTITLE_MIN_ROWS) value = VT_SUBTITLE_MAX_ROWS;
				if (value > VT_SUBTITLE_MAX_ROWS) value = VT_SUBTITLE_MIN_ROWS;
				return vt_preferences_set_subtitle_max_rows(value);
			}
			case 8: {
				static const int order[] = {VT_SUBTITLE_POSITION_BOTTOM,
				    VT_SUBTITLE_POSITION_LOWER, VT_SUBTITLE_POSITION_LOW,
				    VT_SUBTITLE_POSITION_CENTER, VT_SUBTITLE_POSITION_TOP};
				int index = 0;
				while (index < 4 && order[index] != vt_preferences_subtitle_position()) index++;
				return vt_preferences_set_subtitle_position(order[(index + (direction < 0 ? 4 : 1)) % 5]);
			}
		}
	} else if (tab == TAB_INTERFACE) {
		if(row==4){
   int theme=(vt_preferences_interface_theme()+(direction<0?VT_UI_THEME_COUNT-1:1))%VT_UI_THEME_COUNT;
   int result=vt_preferences_set_interface_theme(theme);
   if(result==0){ui_theme_apply(theme);ui_runtime_prepare_theme();}
   return result;
  }
		if (row == 3) return ui_home_tabs_configure();
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
		} else if (row == 3 || row == 4) {
            int hardware = row == 3;
            int current = vt_preferences_decoder_cpu_mhz(hardware);
            const int choices[] = {222,333,444};
            int index = current == 222 ? 0 : current == 333 ? 1 : 2;
            return vt_preferences_set_decoder_cpu_mhz(hardware, choices[(index + (direction < 0 ? 2 : 1)) % 3]);
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
	return tab == TAB_SYSTEM ? 4 : tab == TAB_SUBTITLES ? SUBTITLE_VISIBLE_ROWS : SETTINGS_VISIBLE_ROWS;
}

static UiScrollView settings_scroll;
static int settings_scroll_tab=-1;
static void settings_scroll_context(int tab){
    if(settings_scroll_tab!=tab){ui_scroll_reset(&settings_scroll,0);settings_scroll_tab=tab;}
}
static void draw_subtitle_preview(vita2d_font *label_font) {
	const int x = SUBTITLE_PREVIEW_X, y = SUBTITLE_PREVIEW_Y;
	const int w = SUBTITLE_PREVIEW_W, h = SUBTITLE_PREVIEW_H;
	/* A complete Vita viewport, uniformly scaled instead of vertically cropped. */
	ui_panel(x - 2, y - 2, w + 4, h + 4, VT_THEME_BG, VT_THEME_COLD_DIM, 0);
	if (label_font)
		ui_font_draw_text_exact(label_font, x, y - 15, UI_MENU_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_SETTINGS_SUBTITLE_PREVIEW));
	for (int i = 0; i < 10; i++) {
		int bh = 20 + (i * 29) % 84;
		vita2d_draw_rectangle(x + i * (w/10), y + h - bh, w/10-8, bh,
		                      i & 1 ? RGBA8(4, 17, 24, 255) : RGBA8(7, 25, 34, 255));
	}
	static const int widths[] = {60, 75, 88, 96};
	int text_w = w * widths[vt_preferences_subtitle_max_width()] / 100;
	int left = x + (w - text_w) / 2;
	vita2d_draw_line(left, y + 2, left, y + h - 2, VT_THEME_COLD_DIM);
	vita2d_draw_line(left + text_w, y + 2, left + text_w, y + h - 2, VT_THEME_COLD_DIM);
	ui_subtitle_draw("Signal // 記憶 // 기억 // Память\n接続は安定しています。\n네트워크 신호가 안정적입니다.\nСистема готова к воспроизведению.", x, y, w, h);
}

int ui_settings_show_controls_reference(void) {
	return ui_controls_guide_run(
	    vt_i18n_str(VT_STR_SECTIONS_GUIDE),
	    vt_i18n_str(VT_STR_SETTINGS_CONTROLS_REFERENCE_IT));
}

static void draw_screen(int tab, int cursor, int focus_tabs, const UiFocusMotion *focus,
	                    UiSectionsSidebar *sidebar) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	SettingRow rows[9];
	int row_count = rows_for_tab(tab, rows);
	int row_limit = visible_rows_for_tab(tab);
	int first_row=ui_list_scroll_first(&settings_scroll,ROW_STEP);
	int viewport_height=row_limit*ROW_STEP-8;
    int visible_rows=ui_list_scroll_end(&settings_scroll,ROW_STEP,viewport_height,row_count)-first_row;
	int page_has_focus = !ui_mini_player_input_locked() && !sidebar->open &&
	                     sidebar->animation <= 0.01f;
	int row_width = tab == TAB_SUBTITLES ? SUBTITLE_ROW_W : ROW_W;
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
    ui_menu_surface(0,54,960,490,1);
	ui_brand_draw_breadcrumbs(vt_i18n_str(VT_STR_SECTIONS_SETTINGS), NULL, tab_label(tab));

	for (int i = 0; i < TAB_COUNT; i++) {
		int x = TAB_X + i * (TAB_W + 8);
		ui_menu_text(x+14,TAB_Y+27,TAB_W-28,tab_label(i),i==tab?UI_MENU_ACCENT:UI_MENU_MUTED,UI_FONT_SMALL);
        if(i==tab)vita2d_draw_rectangle(x+14,TAB_Y+TAB_H-2,TAB_W-28,2,UI_MENU_ACCENT);
	}

	vita2d_set_clip_rectangle(ROW_X,ROW_Y,ROW_X+row_width,ROW_Y+viewport_height);
    vita2d_enable_clipping();
    if(page_has_focus&&!focus_tabs)ui_menu_focus_draw(focus);
	for (int slot = 0; slot < visible_rows; slot++) {
		int i = first_row + slot;
		int y=ROW_Y+i*ROW_STEP-settings_scroll.position;
        ui_menu_row(ROW_X,y,row_width,ROW_STEP,page_has_focus&&!focus_tabs&&i==cursor,1);
		if (body) ui_menu_text(ROW_X + 16,
		    y + (tab == TAB_SUBTITLES ? 22 : 35),
		    tab == TAB_SUBTITLES ? row_width-32 : row_width/2-32,
		    rows[i].label, UI_MENU_TEXT, UI_FONT_BODY);
		if (rows[i].kind == ROW_TOGGLE)
			draw_toggle(ROW_X + row_width - 78, y + 14, rows[i].value);
		else if (rows[i].kind == ROW_LANGUAGE)
			draw_value(small, y, row_width, language_label(rows[i].value));
        else if (rows[i].kind == ROW_INTERFACE_THEME) {
            const char *name = ui_theme_name(rows[i].value);
#ifdef VMD_ENABLE_PAF_NATIVE
            if (rows[i].value == VT_UI_THEME_LEGACY && ui_native_vita_error())
                name = vt_i18n_str(VT_STR_SETTINGS_THEME_VMD_RECOVERY);
#endif
            draw_value(small, y, row_width, name);
        }
        else if(tab==TAB_INTERFACE&&i==3)draw_value(small,y,row_width,ui_theme_name(ui_theme_id()));
		else if (rows[i].kind == ROW_CLOCK)
			draw_value(small, y, row_width, rows[i].value == VT_CLOCK_SOURCE_APP
			                       ? vt_i18n_str(VT_STR_SETTINGS_CLOCK_SOURCE_APP)
			                       : vt_i18n_str(VT_STR_SETTINGS_CLOCK_SOURCE_PSVSHELL));
        else if (rows[i].kind == ROW_CPU) {
            char value[24]; snprintf(value, sizeof(value), "%d MHz", rows[i].value);
            draw_value(small, y, row_width, value);
        }
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
		else if (rows[i].kind == ROW_SUBTITLE_BORDER)
			draw_value(small, y, row_width, subtitle_border_label(rows[i].value));
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
    vita2d_disable_clipping();
    ui_scroll_rail(ROW_X+row_width-5,ROW_Y,viewport_height,settings_scroll.position,viewport_height,row_count*ROW_STEP);
	if (tab == TAB_SYSTEM && small && cursor == 2) {
		char path[192];
		snprintf(path, sizeof(path), vt_i18n_str(VT_STR_SETTINGS_PASSWORD_PATH),
		         VITAMEDIADECK_NETWORK_PASSWORDS_PATH);
		ui_font_draw_text_exact(small, ROW_X + 12, 394, VT_THEME_WARNING,
		                  UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_SETTINGS_PASSWORD_PLAINTEXT_WARNING));
		ui_font_draw_text_exact(small, ROW_X + 12, 418, UI_MENU_MUTED,
		                  UI_FONT_SMALL, path);
	}
    if (tab == TAB_SYSTEM && small && cursor != 2) {
        ui_menu_text(ROW_X+12,394,ROW_W-24,vt_i18n_str(VT_STR_SETTINGS_CLOCK_PRIORITY),UI_MENU_MUTED,UI_FONT_SMALL);
    }
	if (tab == TAB_SUBTITLES) {
		draw_subtitle_preview(small);
	} else if (small) {
		const char *footer = vt_i18n_str(VT_STR_SETTINGS_FOOTER_HINT);
#ifdef VMD_ENABLE_PAF_NATIVE
        if (tab == TAB_INTERFACE && ui_native_vita_error())
            footer = vt_i18n_str(VT_STR_SETTINGS_THEME_VMD_RESTART_HINT);
#endif
#ifdef VMD_ENABLE_PAF_NATIVE
#endif
		char fitted[512];
		ui_font_fit_text(small, UI_FONT_SMALL, footer, fitted, sizeof(fitted),
		                 ROW_W);
		ui_font_draw_text_exact(small, ROW_X, 462, UI_MENU_MUTED,
		                  UI_FONT_SMALL, fitted);
	}

	if(page_has_focus){
        if(!focus_tabs){vita2d_set_clip_rectangle(ROW_X,ROW_Y,ROW_X+row_width,ROW_Y+viewport_height);vita2d_enable_clipping();}
        if(focus_tabs)ui_menu_focus_draw(focus);
        vita2d_disable_clipping();
    }
	(void)focus_tabs;
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
	int tab = 0, cursor = 0, focus_tabs = 1;
    settings_scroll_tab=-1;settings_scroll_context(tab);
	UiFocusMotion focus = {0};
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
		if (!ui_mini_player_input_locked() &&
		    ui_brand_sections_hit(touch_flags, touch.x, touch.y)) {
			pressed |= SCE_CTRL_LTRIGGER;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
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
			    &nav_repeat, pressed, ctrl.buttons, ctrl.lx, focus_tabs?ctrl.ly:128,
			    SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
			if (nav & SCE_CTRL_LEFT) {
				tab = tab > 0 ? tab - 1 : TAB_COUNT - 1;
				cursor = 0;
				focus_tabs = 1;
			}
			if (nav & SCE_CTRL_RIGHT) {
				tab = (tab + 1) % TAB_COUNT;
				cursor = 0;
				focus_tabs = 1;
			}
			SettingRow rows[9];
			int count = rows_for_tab(tab, rows);
			if (nav & SCE_CTRL_UP) {
				if (!focus_tabs && cursor > 0) cursor--;
				else focus_tabs = 1;
			}
			if (nav & SCE_CTRL_DOWN) {
				if (focus_tabs) focus_tabs = 0;
				else if (cursor + 1 < count) cursor++;
			}
			if ((pressed & SCE_CTRL_CROSS) && focus_tabs) focus_tabs = 0;
			else if (pressed & SCE_CTRL_CROSS) {
				if (tab == TAB_CONTROLS && cursor == 1) {
					int reference_section = ui_settings_show_controls_reference();
					if (reference_section != UI_SECTION_NONE)
						return reference_section;
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
				focus_tabs = 1;
			}
            settings_scroll_context(tab);
            if(!focus_tabs || ((touch_flags&UI_TOUCH_EVENT_DOWN)&&touch.down_y>=ROW_Y))
                ui_list_scroll_input(&settings_scroll,count,ROW_STEP,ROW_X,ROW_Y,
                    tab==TAB_SUBTITLES?SUBTITLE_ROW_W:ROW_W,visible_rows_for_tab(tab)*ROW_STEP-8,
                    nav,ctrl.buttons,focus_tabs?128:ctrl.ly,&touch_flags,&touch,&cursor);
			if (touch_flags & UI_TOUCH_EVENT_TAP) {
				for (int i = 0; i < TAB_COUNT; i++) {
					int x = TAB_X + i * (TAB_W + 8);
					if (ui_touch_hit_rect(touch.x, touch.y, x, TAB_Y, TAB_W, TAB_H)) {
						tab = i;
						cursor = 0;
						focus_tabs = 1;
					}
				}
				count = rows_for_tab(tab, rows);
				int row_limit = visible_rows_for_tab(tab);
				settings_scroll_context(tab);
                int first_row=ui_list_scroll_first(&settings_scroll,ROW_STEP);
				int visible_rows = count - first_row;
				if(visible_rows>row_limit+1)visible_rows=row_limit+1;
				for (int slot = 0; slot < visible_rows; slot++) {
					int i = first_row + slot;
					int row_width = tab == TAB_SUBTITLES
					              ? SUBTITLE_ROW_W : ROW_W;
					if (touch.y>=ROW_Y&&touch.y<ROW_Y+row_limit*ROW_STEP-8&&ui_touch_hit_rect(touch.x, touch.y, ROW_X,
					                      ROW_Y+i*ROW_STEP-settings_scroll.position, row_width, ROW_H)) {
						cursor = i;
						focus_tabs = 0;
						if (tab == TAB_CONTROLS && cursor == 1) {
							int reference_section = ui_settings_show_controls_reference();
							if (reference_section != UI_SECTION_NONE)
								return reference_section;
						}
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
		SettingRow focus_rows[9];
		int focus_count = rows_for_tab(tab, focus_rows);
        (void)focus_count;
        settings_scroll_context(tab);
		if (focus_tabs) ui_focus_motion_tick(&focus, TAB_X+tab*(TAB_W+8), TAB_Y, TAB_W, TAB_H);
		else ui_focus_motion_tick(&focus, ROW_X, ROW_Y+cursor*ROW_STEP-settings_scroll.position,
		    tab==TAB_SUBTITLES?SUBTITLE_ROW_W:ROW_W, ROW_H);
		draw_screen(tab, cursor, focus_tabs, &focus, &sidebar);
		sceKernelDelayThread(vt_preferences_reduce_motion() ? 16000 : 1000);
	}
}
