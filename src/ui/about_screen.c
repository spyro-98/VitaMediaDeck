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
#include "ui/components.h"
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

#define SCROLL_PAGE_STEP (VIEW_H - 40)
#define SCROLL_SPEED_PX_PER_SECOND 290.0f

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
		                      i == 0 ? VT_THEME_SURFACE_RAISED : COLOR_CARD);
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
	float scroll_position = 0.0f;
	uint64_t scroll_tick_us = sceKernelGetProcessTimeWide();
	int content_h = VIEW_H; /* recomputed every frame from the real layout */
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	int drag_active = 0;
	int drag_start_y = 0;
	int drag_start_scroll = 0;
	int clock_source = vt_preferences_clock_source();
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_INFO);
	int result = UI_SECTION_NONE;

	for (;;) {
		ui_mini_player_pump();
		uint64_t scroll_now_us = sceKernelGetProcessTimeWide();
		uint64_t scroll_elapsed_us = scroll_now_us >= scroll_tick_us
		                           ? scroll_now_us - scroll_tick_us : 0;
		if (scroll_elapsed_us > 50000ULL) scroll_elapsed_us = 50000ULL;
		scroll_tick_us = scroll_now_us;
		float scroll_delta_seconds = (float)scroll_elapsed_us / 1000000.0f;
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
		int sidebar_was_open = sidebar.open;
		int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
		                                                 ctrl.buttons, ctrl.ly);
		int sidebar_owned_frame = sidebar.open || sidebar_was_open;
		if (sidebar_owned_frame) {
			int touched_section = ui_sections_sidebar_handle_touch(
			    &sidebar, touch_flags, touch.x, touch.y);
			if (touched_section != UI_SECTION_NONE) section = touched_section;
			touch_flags = UI_TOUCH_EVENT_NONE;
			drag_active = 0;
		} else if (sidebar.animation <= 0.01f &&
		           ui_mini_player_handle_touch(touch_flags, &touch))
			touch_flags = UI_TOUCH_EVENT_NONE;
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) {
			result = section;
			break;
		}
		if (!sidebar.open && sidebar.animation > 0.01f) {
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
			drag_active = 0;
		}
		int page_owns_input = !sidebar_owned_frame && !sidebar.open &&
		                      sidebar.animation <= 0.01f;
		if (!page_owns_input) {
			pressed = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
			drag_active = 0;
		}
		int open_logs = page_owns_input &&
		    ((pressed & SCE_CTRL_RIGHT) || ctrl.lx > 191 ||
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

		if (page_owns_input && (pressed & SCE_CTRL_CIRCLE)) break;

		int max_scroll = content_h > VIEW_H ? content_h - VIEW_H : 0;
		/* Express scrolling as pixels per second so a slow decode/UI frame does
		 * not accelerate the page. Digital and analog are a single direction. */
		if (page_owns_input) {
			int scroll_direction = (ctrl.buttons & SCE_CTRL_DOWN) || ctrl.ly > 191
			                     ? 1
			                     : (ctrl.buttons & SCE_CTRL_UP) || ctrl.ly < 64
			                         ? -1 : 0;
			scroll_position += scroll_direction * SCROLL_SPEED_PX_PER_SECOND *
			                   scroll_delta_seconds;
			if (pressed & SCE_CTRL_RTRIGGER)
				scroll_position += (float)SCROLL_PAGE_STEP;
		}

		/* Touch drag: natural direction (drag up = read further down). */
		if (touch_flags & UI_TOUCH_EVENT_DOWN) {
			drag_active = 1;
			drag_start_y = touch.y;
			drag_start_scroll = (int)(scroll_position + 0.5f);
		}
		if (drag_active && (touch_flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD)))
			scroll_position = (float)(drag_start_scroll + (drag_start_y - touch.y));
		if (touch_flags & UI_TOUCH_EVENT_UP) drag_active = 0;

		if (scroll_position > (float)max_scroll) scroll_position = (float)max_scroll;
		if (scroll_position < 0.0f) scroll_position = 0.0f;
		scroll = (int)(scroll_position + 0.5f);

		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		vita2d_font *display = ui_runtime_font(UI_FONT_DISPLAY);
		vita2d_texture *logo = ui_runtime_logo();

		vita2d_start_drawing();
		vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);

		vita2d_set_clip_rectangle(0, VIEW_TOP, SCREEN_WIDTH, VIEW_BOTTOM);
		vita2d_enable_clipping();

		/* y walks the virtual layout; -scroll maps it into the viewport. */
		int y = VIEW_TOP + 16 - scroll;

		/* Compact two-panel composition: identity and performance are distinct
		 * information groups, and both remain visible without wasting a column. */
		ui_panel(32, y - 8, 896, 144, VT_THEME_SURFACE,
		         VT_THEME_BLUE_LIGHT, 0);
		if (logo) {
			unsigned int lw = vita2d_texture_get_width(logo);
			unsigned int lh = vita2d_texture_get_height(logo);
			if (lw > 0 && lh > 0) {
				float scale_w = 96.0f / (float)lw;
				float scale_h = 96.0f / (float)lh;
				float scale = scale_w < scale_h ? scale_w : scale_h;
				vita2d_draw_texture_scale(logo, 52.0f, (float)y + 10.0f,
				                          scale, scale);
			}
		}
		if (display) {
			ui_font_draw_text(display, 174, y + 38, COLOR_TEXT,
			                     UI_FONT_DISPLAY, "VitaWave");
		}
		if (small) {
			ui_font_draw_text(small, 174, y + 68, COLOR_MUTED, UI_FONT_SMALL,
			                     vt_i18n_str(VT_STR_ABOUT_TAGLINE));
			ui_font_draw_text(small, 174, y + 94, COLOR_CYAN, UI_FONT_SMALL,
			                     vt_i18n_str(VT_STR_ABOUT_VERSION));
			ui_font_draw_text(small, 174, y + 120, COLOR_MUTED, UI_FONT_SMALL,
			                     vt_i18n_str(VT_STR_ABOUT_DEVELOPER));
		}
		y += 154;

		/* Performance contract kept visible in-app. The explicit Settings value
		 * controls the complete video clock policy. */
		ui_panel(32, y, 896, 174, VT_THEME_SURFACE_RAISED,
		         VT_THEME_BLUE_BRIGHT, 0);
		if (body) {
			ui_font_draw_text(body, 56, y + 35, COLOR_TEXT,
			                     UI_FONT_BODY,
			                     vt_i18n_str(VT_STR_ABOUT_SPECS_TITLE));
		}
		if (small) {
			draw_legend_line(small, 56, y + 67,
			                 vt_i18n_str(VT_STR_ABOUT_SPECS_CPU_DEFAULT));
			draw_legend_line(small, 56, y + 94,
			                 vt_i18n_str(VT_STR_ABOUT_SPECS_OTHER_DEFAULT));
			char specs_line[160];
			snprintf(specs_line, sizeof(specs_line),
			         vt_i18n_str(VT_STR_ABOUT_SPECS_PRIORITY),
			         clock_source == VT_CLOCK_SOURCE_APP
			             ? vt_i18n_str(VT_STR_SETTINGS_CLOCK_SOURCE_APP)
			             : vt_i18n_str(VT_STR_SETTINGS_CLOCK_SOURCE_PSVSHELL));
			draw_legend_line(small, 56, y + 121, specs_line);
			snprintf(specs_line, sizeof(specs_line),
			         vt_i18n_str(VT_STR_ABOUT_SPECS_CURRENT),
			         scePowerGetArmClockFrequency(),
			         scePowerGetGpuClockFrequency(),
			         scePowerGetBusClockFrequency(),
			         scePowerGetGpuXbarClockFrequency());
			draw_legend_line(small, 56, y + 148, specs_line);
		}
		y += 188;
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
			int footer_y = ui_mini_player_top() - 12;
			if (footer_y > FOOTER_Y) footer_y = FOOTER_Y;
			ui_font_draw_text(small, MARGIN_X, footer_y, COLOR_MUTED,
			                     UI_FONT_SMALL, vt_i18n_str(VT_STR_ABOUT_FOOTER_HINT));
		}
		ui_mini_player_draw();
		if (sidebar.animation > 0.01f)
			ui_sections_sidebar_draw(sidebar.cursor, sidebar.animation,
			                         sidebar.open ? sidebar.focus_cursor : -1.0f);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
	}
	return result;
}
