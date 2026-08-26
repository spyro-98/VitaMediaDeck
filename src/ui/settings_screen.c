#include "ui/settings_screen.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
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
#define TAB_W 216
#define TAB_H 40
#define ROW_X 44
#define ROW_Y 130
#define ROW_W 872
#define ROW_H 56
#define ROW_STEP 64

enum {
	TAB_PLAYBACK = 0,
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

enum { ROW_TOGGLE, ROW_LANGUAGE, ROW_CLOCK, ROW_MAPPING, ROW_ACTION };

static const char *tab_label(int tab) {
	static const VtStringId labels[TAB_COUNT] = {
		VT_STR_SETTINGS_TAB_PLAYBACK,
		VT_STR_SETTINGS_TAB_INTERFACE,
		VT_STR_SETTINGS_TAB_SYSTEM,
		VT_STR_SETTINGS_TAB_CONTROLS
	};
	return tab >= 0 && tab < TAB_COUNT ? vt_i18n_str(labels[tab]) : "";
}

static void draw_toggle(float x, float y, int enabled) {
	vita2d_draw_rectangle(x, y, 54, 28,
	                      enabled ? VT_THEME_BLUE : VT_THEME_BORDER);
	vita2d_draw_fill_circle(x + (enabled ? 40.0f : 14.0f), y + 14.0f,
	                        10.0f, VT_THEME_TEXT);
}

static void draw_value(vita2d_font *small, int y, const char *value) {
	if (!small || !value) return;
	float width = ui_font_text_width(small, UI_FONT_SMALL, value);
	ui_font_draw_text(small, ROW_X + ROW_W - width - 22, y + 34,
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

static int rows_for_tab(int tab, SettingRow rows[8]) {
	if (tab == TAB_PLAYBACK) {
		rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_FILL_SCREEN), vt_preferences_fill_screen(), ROW_TOGGLE };
		rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_LOOP_PLAYBACK), vt_preferences_loop_enabled(), ROW_TOGGLE };
		rows[2] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_KEEP_MUSIC_AWAKE), vt_preferences_music_keep_display_awake(), ROW_TOGGLE };
		return 3;
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
		return 2;
	}
	rows[0] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_CONTROL_MAPPING),
	                        vt_preferences_player_swap_shoulders(), ROW_MAPPING };
	rows[1] = (SettingRow){ vt_i18n_str(VT_STR_SETTINGS_CONTROL_REFERENCE), 0, ROW_ACTION };
	return 2;
}

static int apply_row(int tab, int row, int direction) {
	if (tab == TAB_PLAYBACK) {
		switch (row) {
			case 0: return vt_preferences_set_fill_screen(!vt_preferences_fill_screen());
			case 1: return vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled());
			case 2: return vt_preferences_set_music_keep_display_awake(!vt_preferences_music_keep_display_awake());
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
		}
	} else if (tab == TAB_CONTROLS && row == 0) {
		return vt_preferences_set_player_swap_shoulders(
		       !vt_preferences_player_swap_shoulders());
	}
	return 0;
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
	int page_has_focus = !ui_mini_player_input_locked() && !sidebar->open &&
	                     sidebar->animation <= 0.01f;
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header(NULL);

	for (int i = 0; i < TAB_COUNT; i++) {
		int x = TAB_X + i * (TAB_W + 8);
		vita2d_draw_rectangle(x, TAB_Y, TAB_W, TAB_H,
		                      i == tab ? VT_THEME_SURFACE_RAISED : VT_THEME_SURFACE);
		if (i == tab)
			vita2d_draw_rectangle(x, TAB_Y + TAB_H - 3, TAB_W, 3,
			                      VT_THEME_BLUE_LIGHT);
		if (body) ui_font_draw_text(body, x + 16, TAB_Y + 27,
		                             i == tab ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
			                             UI_FONT_BODY, tab_label(i));
	}
	if (page_has_focus)
		ui_focus_glow_draw(ROW_X, ROW_Y + focus * ROW_STEP, ROW_W, ROW_H,
		                   sceKernelGetProcessTimeWide(), ROW_Y, 432);

	for (int i = 0; i < row_count; i++) {
		int y = ROW_Y + i * ROW_STEP;
		vita2d_draw_rectangle(ROW_X, y, ROW_W, ROW_H, VT_THEME_SURFACE);
		vita2d_draw_rectangle(ROW_X, y, 4, ROW_H, VT_THEME_BORDER);
		if (body) ui_font_draw_text(body, ROW_X + 22, y + 35,
		                             page_has_focus && i == cursor
		                                 ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
		                             UI_FONT_BODY, rows[i].label);
		if (rows[i].kind == ROW_TOGGLE)
			draw_toggle(ROW_X + ROW_W - 78, y + 14, rows[i].value);
		else if (rows[i].kind == ROW_LANGUAGE)
			draw_value(small, y, language_label(rows[i].value));
		else if (rows[i].kind == ROW_CLOCK)
			draw_value(small, y, rows[i].value == VT_CLOCK_SOURCE_APP
			                       ? "VitaWave 444/222/222/111"
			                       : vt_i18n_str(VT_STR_SETTINGS_CLOCK_SOURCE_PSVSHELL));
		else if (rows[i].kind == ROW_MAPPING)
			draw_value(small, y, vt_i18n_str(rows[i].value
			    ? VT_STR_SETTINGS_CONTROL_DPAD_PANELS
			    : VT_STR_SETTINGS_CONTROL_SHOULDERS_PANELS));
		else draw_value(small, y, vt_i18n_str(VT_STR_SETTINGS_OPEN));
	}
	if (small)
		ui_font_draw_text(small, ROW_X, 462, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_SETTINGS_FOOTER_HINT));

	ui_mini_player_draw();
	if (sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar->cursor, sidebar->animation,
		                         sidebar->open ? sidebar->focus_cursor : -1.0f);
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
				for (int i = 0; i < count; i++) {
					if (ui_touch_hit_rect(touch.x, touch.y, ROW_X,
					                      ROW_Y + i * ROW_STEP, ROW_W, ROW_H)) {
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
		float target = (float)cursor;
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
