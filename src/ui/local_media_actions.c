#include "ui/local_media_actions.h"
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include "i18n/i18n.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/focus_glow.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

/* Both grids and lists use this modal. Cancel is initially focused in the
 * destructive confirmation; a held opening button cannot confirm it. */
static int choose_action(const VtLocalMediaItem *item, int confirmation) {
	if (!item || item->is_directory || !item->path[0]) return 0;
	const char *labels[4] = {
		vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_RENAME),
		vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_MOVE),
		vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_DELETE),
		vt_i18n_str(VT_STR_LOCAL_MEDIA_CANCEL)
	};
	int actions[4] = {UI_LOCAL_MEDIA_ACTION_RENAME, UI_LOCAL_MEDIA_ACTION_MOVE,
	                  UI_LOCAL_MEDIA_ACTION_DELETE, 0};
	int count = 4, cursor = 0;
	if (confirmation) {
		labels[0] = labels[3]; actions[0] = 0;
		labels[1] = labels[2]; actions[1] = 1;
		count = 2;
	}
	SceCtrlData previous = {0};
	sceCtrlPeekBufferPositive(0, &previous, 1);
	UiNavRepeat repeat;
	ui_nav_repeat_reset(&repeat);
	ui_touch_reset();
	for (;;) {
		ui_mini_player_pump();
		SceCtrlData controls = {0};
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		unsigned nav = ui_nav_repeat_update(&repeat, pressed, controls.buttons,
		    controls.lx, controls.ly, SCE_CTRL_UP | SCE_CTRL_DOWN);
		if ((nav & SCE_CTRL_UP) && cursor > 0) cursor--;
		if ((nav & SCE_CTRL_DOWN) && cursor + 1 < count) cursor++;
		UiTouchEvent touch;
		unsigned flags = ui_touch_poll(&touch);
		if (flags & UI_TOUCH_EVENT_TAP)
			for (int i = 0; i < count; i++)
				if (ui_touch_hit_rect(touch.x, touch.y, 200, 230 + i * 64, 560, 54)) {
					cursor = i;
					pressed |= SCE_CTRL_CROSS;
				}
		if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) {
			ui_touch_reset();
			return pressed & SCE_CTRL_CIRCLE ? 0 : actions[cursor];
		}
		vita2d_start_drawing();
		vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
		ui_brand_draw_header_placeholder(NULL, vt_i18n_str(confirmation
		    ? VT_STR_LOCAL_MEDIA_DELETE_TITLE : VT_STR_LOCAL_MEDIA_ACTIONS));
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		ui_panel(66, 88, 828, 118, VT_THEME_SURFACE_RAISED, VT_THEME_BLUE_LIGHT, 0);
		char fitted[VT_LOCAL_MEDIA_PATH_MAX];
		if (body) {
			ui_font_fit_text(body, UI_FONT_BODY, item->name, fitted, sizeof(fitted), 792);
			ui_font_draw_text(body, 84, 119, VT_THEME_TEXT, UI_FONT_BODY, fitted);
		}
		if (small) {
			ui_font_fit_text(small, UI_FONT_SMALL, item->path, fitted, sizeof(fitted), 792);
			ui_font_draw_text(small, 84, 149, VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
			if (confirmation) ui_font_draw_text(small, 84, 185, VT_THEME_TEXT,
			    UI_FONT_SMALL, vt_i18n_str(VT_STR_LOCAL_MEDIA_DELETE_ONLY_FILE));
		}
		for (int i = 0; i < count; i++)
			ui_action_button(200, 230 + i * 64, 560, 54,
			    cursor == i ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			    "Cross", labels[i], cursor == i);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
		sceKernelDelayThread(16000);
	}
}

int ui_local_media_actions(const VtLocalMediaItem *item) {
	return choose_action(item, 0);
}

int ui_local_media_confirm_delete(const VtLocalMediaItem *item) {
	return choose_action(item, 1);
}
