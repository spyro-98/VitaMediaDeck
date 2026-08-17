#include "ui/settings_screen.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "ui/brand.h"
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

enum { ROW_TOGGLE, ROW_LANGUAGE, ROW_CLOCK, ROW_ACTION };

static const char *const g_tab_labels[TAB_COUNT] = {
	"Playback", "Interface", "System", "Controls"
};

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
	static const char *const labels[] = {
		"System", "English", "Italiano"
	};
	return language >= 0 && language < language_count() ? labels[language] : labels[0];
}

static int rows_for_tab(int tab, SettingRow rows[8]) {
	if (tab == TAB_PLAYBACK) {
		rows[0] = (SettingRow){ "Fill video to screen", vt_preferences_fill_screen(), ROW_TOGGLE };
		rows[1] = (SettingRow){ "Loop playback", vt_preferences_loop_enabled(), ROW_TOGGLE };
		rows[2] = (SettingRow){ "Keep display awake for music", vt_preferences_music_keep_display_awake(), ROW_TOGGLE };
		return 3;
	}
	if (tab == TAB_INTERFACE) {
		rows[0] = (SettingRow){ "Language", vt_preferences_language(), ROW_LANGUAGE };
		rows[1] = (SettingRow){ "Reduce motion", vt_preferences_reduce_motion(), ROW_TOGGLE };
		rows[2] = (SettingRow){ "Always show player status", vt_preferences_player_status_always_visible(), ROW_TOGGLE };
		return 3;
	}
	if (tab == TAB_SYSTEM) {
		rows[0] = (SettingRow){ "Clock control", vt_preferences_clock_source(), ROW_CLOCK };
		rows[1] = (SettingRow){ "Write diagnostic logs", vt_preferences_disk_logs_enabled(), ROW_TOGGLE };
		return 2;
	}
	rows[0] = (SettingRow){ "Open complete controls reference", 0, ROW_ACTION };
	return 1;
}

static void apply_row(int tab, int row, int direction) {
	if (tab == TAB_PLAYBACK) {
		switch (row) {
			case 0: vt_preferences_set_fill_screen(!vt_preferences_fill_screen()); break;
			case 1: vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled()); break;
			case 2: vt_preferences_set_music_keep_display_awake(!vt_preferences_music_keep_display_awake()); break;
		}
	} else if (tab == TAB_INTERFACE) {
		if (row == 0) {
			int language = vt_preferences_language() + (direction < 0 ? -1 : 1);
			if (language < 0) language = language_count() - 1;
			if (language >= language_count()) language = 0;
			vt_i18n_set_language(language);
		} else if (row == 1) {
			vt_preferences_set_reduce_motion(!vt_preferences_reduce_motion());
		} else if (row == 2) {
			vt_preferences_set_player_status_always_visible(
			    !vt_preferences_player_status_always_visible());
		}
	} else if (tab == TAB_SYSTEM) {
		if (row == 0) {
			int value = vt_preferences_clock_source() == VT_CLOCK_SOURCE_APP
			          ? VT_CLOCK_SOURCE_PSVSHELL : VT_CLOCK_SOURCE_APP;
			vt_preferences_set_clock_source(value);
		} else if (row == 1) {
			vt_preferences_set_disk_logs_enabled(!vt_preferences_disk_logs_enabled());
		}
	}
}

void ui_settings_show_controls_reference(void) {
	static const char controls[] =
		"NORMAL NAVIGATION\n\n"
		"D-pad / left stick  Move focus and scroll\n"
		"X                   Open or confirm\n"
		"Circle              Back or close a panel\n"
		"L1                  Open or close navigation\n"
		"Square              Context action\n"
		"Triangle            Edit the selected network source\n\n"
		"VIDEO PLAYER\n\n"
		"X                   Pause or resume\n"
		"Circle              Stop and return\n"
		"D-pad left/right    Seek backward or forward\n"
		"Left stick          Seek\n"
		"Right stick         Volume\n"
		"Touch timeline      Seek to a position\n"
		"Touch video         Show or hide controls\n\n"
		"MUSIC AND MINI PLAYER\n\n"
		"X / touch           Pause or resume\n"
		"Left/right          Seek by ten seconds\n"
		"Select held         Lock or unlock input\n";
	ui_text_reader_run_tabbed("Settings", "Controls", controls);
}

static void draw_screen(int tab, int cursor, float focus,
	                    UiSectionsSidebar *sidebar) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	SettingRow rows[8];
	int row_count = rows_for_tab(tab, rows);
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_brand_draw_header(NULL);

	for (int i = 0; i < TAB_COUNT; i++) {
		int x = TAB_X + i * (TAB_W + 8);
		vita2d_draw_rectangle(x, TAB_Y, TAB_W, TAB_H,
		                      i == tab ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE);
		if (i == tab)
			vita2d_draw_rectangle(x, TAB_Y + TAB_H - 3, TAB_W, 3,
			                      VT_THEME_BLUE_LIGHT);
		if (body) ui_font_draw_text(body, x + 16, TAB_Y + 27,
		                             i == tab ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
		                             UI_FONT_BODY, g_tab_labels[i]);
	}

	for (int i = 0; i < row_count; i++) {
		int y = ROW_Y + i * ROW_STEP;
		float distance = focus - (float)i;
		if (distance < 0) distance = -distance;
		float selected = distance >= 1.0f ? 0.0f : 1.0f - distance;
		unsigned int surface = selected > 0.01f
		    ? RGBA8(12, 38, 67, (unsigned int)(170 + selected * 85.0f))
		    : VT_THEME_SURFACE;
		vita2d_draw_rectangle(ROW_X, y, ROW_W, ROW_H, surface);
		if (selected > 0.01f)
			vita2d_draw_rectangle(ROW_X, y, 4, ROW_H, VT_THEME_BLUE_LIGHT);
		if (body) ui_font_draw_text(body, ROW_X + 22, y + 35,
		                             i == cursor ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
		                             UI_FONT_BODY, rows[i].label);
		if (rows[i].kind == ROW_TOGGLE)
			draw_toggle(ROW_X + ROW_W - 78, y + 14, rows[i].value);
		else if (rows[i].kind == ROW_LANGUAGE)
			draw_value(small, y, language_label(rows[i].value));
		else if (rows[i].kind == ROW_CLOCK)
			draw_value(small, y, rows[i].value == VT_CLOCK_SOURCE_APP
			                       ? "VitaTube 444/222/222/111"
			                       : "PSVshell manual");
		else draw_value(small, y, "Open");
	}

	if (sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar->cursor, sidebar->animation,
		                         sidebar->focus_cursor);
	ui_mini_player_draw();
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
	sceCtrlPeekBufferPositive(0, &previous, 1);
	int tab = 0, cursor = 0;
	float focus = 0.0f;
	uint64_t repeat_at = 0;
	unsigned int repeat_button = 0;
	for (;;) {
		ui_mini_player_pump();
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		ui_mini_player_handle_buttons(&pressed);
		UiTouchEvent touch;
		unsigned int touch_flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(touch_flags, &touch))
			touch_flags = UI_TOUCH_EVENT_NONE;
		int was_open = sidebar.open;
		int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
		                                                 ctrl.buttons, ctrl.ly);
		if (sidebar.open || was_open) {
			int touched = ui_sections_sidebar_handle_touch(&sidebar, touch_flags,
			                                                  touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) return section;
		if (!sidebar.open && (pressed & SCE_CTRL_CIRCLE)) return UI_SECTION_NONE;

		if (!sidebar.open) {
			if (pressed & SCE_CTRL_LEFT) {
				tab = tab > 0 ? tab - 1 : TAB_COUNT - 1;
				cursor = 0;
			}
			if (pressed & SCE_CTRL_RIGHT) {
				tab = (tab + 1) % TAB_COUNT;
				cursor = 0;
			}
			unsigned int nav = (ctrl.buttons & SCE_CTRL_UP) || ctrl.ly < 48
			                 ? SCE_CTRL_UP
			                 : ((ctrl.buttons & SCE_CTRL_DOWN) || ctrl.ly > 207
			                    ? SCE_CTRL_DOWN : 0);
			uint64_t now = sceKernelGetProcessTimeWide();
			if (!nav) repeat_button = 0;
			else if (nav != repeat_button) {
				repeat_button = nav;
				repeat_at = now + 280000ULL;
				pressed |= nav;
			} else if (now >= repeat_at) {
				repeat_at = now + 95000ULL;
				pressed |= nav;
			}
			SettingRow rows[8];
			int count = rows_for_tab(tab, rows);
			if ((pressed & SCE_CTRL_UP) && cursor > 0) cursor--;
			if ((pressed & SCE_CTRL_DOWN) && cursor + 1 < count) cursor++;
			if (pressed & SCE_CTRL_CROSS) {
				if (tab == TAB_CONTROLS) {
					ui_settings_show_controls_reference();
					ui_touch_reset();
					sceCtrlPeekBufferPositive(0, &previous, 1);
				} else apply_row(tab, cursor, 1);
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
						if (tab == TAB_CONTROLS) ui_settings_show_controls_reference();
						else apply_row(tab, cursor, 1);
						ui_touch_reset();
						sceCtrlPeekBufferPositive(0, &previous, 1);
						break;
					}
				}
			}
		}
		float target = (float)cursor;
		if (vt_preferences_reduce_motion()) focus = target;
		else focus += (target - focus) * 0.28f;
		draw_screen(tab, cursor, focus, &sidebar);
	}
}
