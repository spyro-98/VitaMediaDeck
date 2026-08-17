#include "ui/about_screen.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/mini_player.h"
#include "ui/log_viewer.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define SCREEN_WIDTH  960
#define SCREEN_HEIGHT 544

#define COLOR_TEXT   VT_THEME_TEXT
#define COLOR_MUTED  VT_THEME_TEXT_MUTED
#define COLOR_CYAN   VT_THEME_BLUE_LIGHT
#define COLOR_BLUE   VT_THEME_BLUE_BRIGHT
#define COLOR_CARD   VT_THEME_SURFACE

/* Scrollable content area between header and footer. The measured virtual
 * layout keeps longer translations from colliding with either one. */
#define VIEW_TOP    (UI_BRAND_HEADER_HEIGHT + 54)
#define FOOTER_Y    514
#define VIEW_BOTTOM (FOOTER_Y - 22)
#define VIEW_H      (VIEW_BOTTOM - VIEW_TOP)

#define TAB_Y             (UI_BRAND_HEADER_HEIGHT + 10)
#define TAB_H             40
#define TAB_BASELINE_Y    (TAB_Y + 27)
#define TAB_UNDERLINE_Y   (TAB_Y + TAB_H - 3)

#define SCROLL_STEP      28
#define SCROLL_PAGE_STEP (VIEW_H - 40)

#define MARGIN_X 40
#define LINE_STEP 27

static void draw_legend_line(vita2d_font *small, int x, int y, const char *text) {
	vita2d_draw_fill_circle((float)x + 3.0f, (float)y - 5.0f, 3.0f, COLOR_BLUE);
	if (small) ui_font_draw_text(small, x + 16, y, COLOR_MUTED, UI_FONT_SMALL, text);
}

static void draw_about_tabs(void) {
	vita2d_font *font = ui_runtime_font(UI_FONT_BODY);
	const char *labels[2] = {
		vt_i18n_str(VT_STR_ABOUT_TAB_SPECS),
		vt_i18n_str(VT_STR_ABOUT_TAB_LOGS)
	};
	for (int i = 0; i < 2; i++) {
		int x = 36 + i * 190;
		vita2d_draw_rectangle(x, TAB_Y, 178, TAB_H,
		                      i == 0 ? RGBA8(13, 55, 94, 255) : COLOR_CARD);
		if (i == 0) vita2d_draw_rectangle(x, TAB_UNDERLINE_Y, 178, 3, COLOR_CYAN);
		if (font) ui_font_draw_text(font, x + 18, TAB_BASELINE_Y,
		                                 i == 0 ? COLOR_TEXT : COLOR_MUTED,
		                                 UI_FONT_BODY, labels[i]);
	}
}

int ui_about_screen(int player_context) {
	if (!ui_runtime_is_ready()) return UI_SECTION_NONE;
	(void)player_context;
	int scroll = 0;
	int content_h = VIEW_H; /* recomputed every frame from the real layout */
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	int drag_active = 0;
	int drag_start_y = 0;
	int drag_start_scroll = 0;
	int clock_source = vt_preferences_clock_source();
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_INFO);
	int result = UI_SECTION_NONE;

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
		int sidebar_was_open = sidebar.open;
		int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
		                                                 ctrl.buttons, ctrl.ly);
		if (sidebar.open || sidebar_was_open) {
			int touched_section = ui_sections_sidebar_handle_touch(
			    &sidebar, touch_flags, touch.x, touch.y);
			if (touched_section != UI_SECTION_NONE) section = touched_section;
			touch_flags = UI_TOUCH_EVENT_NONE;
			drag_active = 0;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) {
			result = section;
			break;
		}
		int open_logs = !sidebar.open &&
		    ((pressed & SCE_CTRL_RIGHT) || ctrl.lx > 207 ||
		     ((touch_flags & UI_TOUCH_EVENT_TAP) &&
		      ui_touch_hit_rect(touch.x, touch.y, 226, TAB_Y, 178, TAB_H)));
		if (open_logs) {
			int log_result = ui_log_viewer_screen();
			ui_touch_reset();
			if (log_result >= UI_SECTION_LOCAL_MEDIA && log_result < UI_SECTION_COUNT) {
				result = log_result;
				break;
			}
			if (log_result == UI_SECTION_NONE) break;
			continue;
		}

		if (pressed & SCE_CTRL_CIRCLE) break;

		int max_scroll = content_h > VIEW_H ? content_h - VIEW_H : 0;
		/* Held, not just newly pressed: holding the stick/d-pad scrolls
		 * continuously instead of one step per press. */
		if (!sidebar.open) {
			if (ctrl.buttons & SCE_CTRL_DOWN) scroll += SCROLL_STEP / 3;
			if (ctrl.buttons & SCE_CTRL_UP) scroll -= SCROLL_STEP / 3;
			if (ctrl.ly > 200) scroll += SCROLL_STEP / 3;
			if (ctrl.ly < 55) scroll -= SCROLL_STEP / 3;
			if (pressed & SCE_CTRL_RTRIGGER) scroll += SCROLL_PAGE_STEP;
		}

		/* Touch drag: natural direction (drag up = read further down). */
		if (touch_flags & UI_TOUCH_EVENT_DOWN) {
			drag_active = 1;
			drag_start_y = touch.y;
			drag_start_scroll = scroll;
		}
		if (drag_active && (touch_flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD)))
			scroll = drag_start_scroll + (drag_start_y - touch.y);
		if (touch_flags & UI_TOUCH_EVENT_UP) drag_active = 0;

		if (scroll > max_scroll) scroll = max_scroll;
		if (scroll < 0) scroll = 0;

		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		vita2d_font *display = ui_runtime_font(UI_FONT_DISPLAY);
		vita2d_texture *logo = ui_runtime_logo();

		vita2d_start_drawing();
		vita2d_clear_screen();

		vita2d_set_clip_rectangle(0, VIEW_TOP, SCREEN_WIDTH, VIEW_BOTTOM);
		vita2d_enable_clipping();

		/* y walks the virtual layout; -scroll maps it into the viewport. */
		int y = VIEW_TOP + 16 - scroll;

		if (logo) {
			unsigned int lw = vita2d_texture_get_width(logo);
			unsigned int lh = vita2d_texture_get_height(logo);
			if (lw > 0 && lh > 0) {
				float scale = 96.0f / (float)lw;
				vita2d_draw_texture_scale(logo, (float)MARGIN_X, (float)y, scale, scale);
				y += (int)((float)lh * scale) + 12;
			}
		}
		if (display) {
			ui_font_draw_text(display, MARGIN_X, y + 22, COLOR_TEXT,
			                     UI_FONT_DISPLAY, "VitaTube");
			y += 40;
		}
		if (small) {
			ui_font_draw_text(small, MARGIN_X, y + 14, COLOR_MUTED, UI_FONT_SMALL,
			                     vt_i18n_str(VT_STR_ABOUT_TAGLINE));
			y += LINE_STEP;
			ui_font_draw_text(small, MARGIN_X, y + 14, COLOR_CYAN, UI_FONT_SMALL,
			                     vt_i18n_str(VT_STR_ABOUT_VERSION));
			y += LINE_STEP;
			ui_font_draw_text(small, MARGIN_X, y + 14, COLOR_MUTED, UI_FONT_SMALL,
			                     vt_i18n_str(VT_STR_ABOUT_DEVELOPER));
			y += LINE_STEP + 18;
		}

		/* Performance contract kept visible in-app. The explicit Settings value
		 * controls the complete video clock policy. */
		if (body) {
			ui_font_draw_text(body, MARGIN_X, y + 16, COLOR_TEXT,
			                     UI_FONT_BODY,
			                     vt_i18n_str(VT_STR_ABOUT_SPECS_TITLE));
			y += 34;
		}
		if (small) {
			draw_legend_line(small, MARGIN_X, y + 14,
			                 vt_i18n_str(VT_STR_ABOUT_SPECS_CPU_DEFAULT));
			y += LINE_STEP;
			draw_legend_line(small, MARGIN_X, y + 14,
			                 vt_i18n_str(VT_STR_ABOUT_SPECS_OTHER_DEFAULT));
			y += LINE_STEP;
			char specs_line[160];
			snprintf(specs_line, sizeof(specs_line),
			         vt_i18n_str(VT_STR_ABOUT_SPECS_PRIORITY),
			         clock_source == VT_CLOCK_SOURCE_APP
			             ? vt_i18n_str(VT_STR_SETTINGS_CLOCK_SOURCE_APP)
			             : vt_i18n_str(VT_STR_SETTINGS_CLOCK_SOURCE_PSVSHELL));
			draw_legend_line(small, MARGIN_X, y + 14, specs_line);
			y += LINE_STEP;
			snprintf(specs_line, sizeof(specs_line),
			         vt_i18n_str(VT_STR_ABOUT_SPECS_CURRENT),
			         scePowerGetArmClockFrequency(),
			         scePowerGetGpuClockFrequency(),
			         scePowerGetBusClockFrequency(),
			         scePowerGetGpuXbarClockFrequency());
			draw_legend_line(small, MARGIN_X, y + 14, specs_line);
			y += LINE_STEP + 18;
		}

		y += 16;
		/* Measured from the layout itself, so adding an information line or
		 * a longer translation keeps scrolling correct with no constant to
		 * update by hand. */
		content_h = y + scroll - VIEW_TOP;

		vita2d_disable_clipping();

		/* Header drawn after the clipped content so the content scrolling
		 * under it is hidden behind the bar, not on top of it. */
		ui_brand_draw_header(NULL);
		draw_about_tabs();

		if (max_scroll > 0) {
			/* Scrollbar: without it there is no hint that more text exists
			 * below — the whole reason the first version looked "broken". */
			const float track_x = SCREEN_WIDTH - 12.0f;
			const float track_h = (float)VIEW_H;
			vita2d_draw_rectangle(track_x, (float)VIEW_TOP, 4.0f, track_h,
			                     RGBA8(24, 38, 60, 255));
			float ratio = (float)VIEW_H / (float)content_h;
			float thumb_h = track_h * ratio;
			if (thumb_h < 24.0f) thumb_h = 24.0f;
			float thumb_y = (float)VIEW_TOP +
			                (track_h - thumb_h) * ((float)scroll / (float)max_scroll);
			vita2d_draw_rectangle(track_x, thumb_y, 4.0f, thumb_h, COLOR_CYAN);
		}

		if (small) {
			ui_font_draw_text(small, MARGIN_X, FOOTER_Y, COLOR_MUTED,
			                     UI_FONT_SMALL, vt_i18n_str(VT_STR_ABOUT_FOOTER_HINT));
		}
		if (sidebar.animation > 0.01f)
			ui_sections_sidebar_draw(sidebar.cursor, sidebar.animation,
			                         sidebar.focus_cursor);
		ui_mini_player_draw();
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
	}
	return result;
}
