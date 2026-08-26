#include "ui/sections_sidebar.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define SCREEN_HEIGHT 544
#define SIDEBAR_WIDTH 286
#define ITEM_X 18
#define ITEM_Y 156
#define ITEM_W 250
#define ITEM_H 40
#define ITEM_STEP 54

#define ANALOG_LOW 64
#define ANALOG_HIGH 191
#define NAV_REPEAT_DELAY_US 280000ULL
#define NAV_REPEAT_INTERVAL_US 95000ULL

void ui_nav_repeat_reset(UiNavRepeat *repeat) {
	if (!repeat) return;
	repeat->direction = 0;
	repeat->repeat_next_us = 0;
}

static unsigned int held_direction(unsigned int buttons,
	                               unsigned char analog_x,
	                               unsigned char analog_y,
	                               unsigned int allowed) {
	/* Digital input wins. It is exact and should never be displaced by a stick
	 * that is merely resting a few units away from its centre. */
	if ((allowed & SCE_CTRL_UP) && (buttons & SCE_CTRL_UP)) return SCE_CTRL_UP;
	if ((allowed & SCE_CTRL_DOWN) && (buttons & SCE_CTRL_DOWN)) return SCE_CTRL_DOWN;
	if ((allowed & SCE_CTRL_LEFT) && (buttons & SCE_CTRL_LEFT)) return SCE_CTRL_LEFT;
	if ((allowed & SCE_CTRL_RIGHT) && (buttons & SCE_CTRL_RIGHT)) return SCE_CTRL_RIGHT;

	int dx = (int)analog_x - 128;
	int dy = (int)analog_y - 128;
	int abs_x = dx < 0 ? -dx : dx;
	int abs_y = dy < 0 ? -dy : dy;
	int horizontal = ((allowed & SCE_CTRL_LEFT) && analog_x < ANALOG_LOW) ||
	                 ((allowed & SCE_CTRL_RIGHT) && analog_x > ANALOG_HIGH);
	int vertical = ((allowed & SCE_CTRL_UP) && analog_y < ANALOG_LOW) ||
	               ((allowed & SCE_CTRL_DOWN) && analog_y > ANALOG_HIGH);
	/* A diagonal stick position resolves to the dominant axis. This makes grid
	 * movement predictable instead of producing two focus changes in one frame. */
	if (horizontal && (!vertical || abs_x > abs_y))
		return dx < 0 ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT;
	if (vertical)
		return dy < 0 ? SCE_CTRL_UP : SCE_CTRL_DOWN;
	return 0;
}

unsigned int ui_nav_repeat_update(UiNavRepeat *repeat,
	                              unsigned int pressed,
	                              unsigned int held_buttons,
	                              unsigned char analog_x,
	                              unsigned char analog_y,
	                              unsigned int allowed_directions) {
	if (!repeat) return pressed & allowed_directions;
	unsigned int direction = held_direction(held_buttons, analog_x, analog_y,
	                                        allowed_directions);
	unsigned int navigation = pressed & allowed_directions;
	uint64_t now = sceKernelGetProcessTimeWide();
	if (!direction) {
		repeat->direction = 0;
	} else if (direction != repeat->direction) {
		repeat->direction = direction;
		repeat->repeat_next_us = now + NAV_REPEAT_DELAY_US;
		navigation |= direction;
	} else if (now >= repeat->repeat_next_us) {
		repeat->repeat_next_us = now + NAV_REPEAT_INTERVAL_US;
		navigation |= direction;
	}
	return navigation;
}

void ui_sections_sidebar_init(UiSectionsSidebar *sidebar, int active) {
	if (!sidebar) return;
	sidebar->open = 0;
	sidebar->active = active >= 0 && active < UI_SECTION_COUNT
	                ? active : UI_SECTION_LOCAL_MEDIA;
	sidebar->cursor = sidebar->active;
	sidebar->animation = 0.0f;
	sidebar->focus_cursor = (float)sidebar->cursor;
	sidebar->repeat_button = 0;
	sidebar->repeat_next_us = 0;
	sidebar->animation_last_us = 0;
}

static int activate(UiSectionsSidebar *sidebar) {
	int selected = sidebar->cursor;
	sidebar->open = 0;
	return selected == sidebar->active ? UI_SECTION_NONE : selected;
}

int ui_sections_sidebar_handle_buttons(UiSectionsSidebar *sidebar,
	                                   unsigned int *pressed,
	                                   unsigned int held_buttons,
	                                   unsigned char analog_y) {
	if (!sidebar || !pressed) return UI_SECTION_NONE;
	if (*pressed & SCE_CTRL_LTRIGGER) {
		sidebar->open = !sidebar->open;
		if (sidebar->open) sidebar->cursor = sidebar->active;
		sidebar->repeat_button = 0;
		*pressed &= ~SCE_CTRL_LTRIGGER;
		return UI_SECTION_NONE;
	}
	if (!sidebar->open) return UI_SECTION_NONE;
	UiNavRepeat repeat = { sidebar->repeat_button, sidebar->repeat_next_us };
	unsigned int navigated = ui_nav_repeat_update(
	    &repeat, *pressed, held_buttons, 128, analog_y,
	    SCE_CTRL_UP | SCE_CTRL_DOWN);
	sidebar->repeat_button = repeat.direction;
	sidebar->repeat_next_us = repeat.repeat_next_us;
	if ((navigated & SCE_CTRL_UP) && sidebar->cursor > 0) sidebar->cursor--;
	if ((navigated & SCE_CTRL_DOWN) && sidebar->cursor + 1 < UI_SECTION_COUNT)
		sidebar->cursor++;
	int selected = (*pressed & SCE_CTRL_CROSS) ? activate(sidebar) : UI_SECTION_NONE;
	if (*pressed & SCE_CTRL_CIRCLE) sidebar->open = 0;
	*pressed &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT |
	              SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE |
	              SCE_CTRL_SQUARE | SCE_CTRL_START | SCE_CTRL_RTRIGGER);
	return selected;
}

int ui_sections_sidebar_handle_touch(UiSectionsSidebar *sidebar,
	                                 unsigned int touch_flags, int x, int y) {
	if (!sidebar || !sidebar->open || !(touch_flags & UI_TOUCH_EVENT_TAP))
		return UI_SECTION_NONE;
	int offset_x = (int)(-(float)SIDEBAR_WIDTH * (1.0f - sidebar->animation));
	for (int i = 0; i < UI_SECTION_COUNT; i++) {
		if (!ui_touch_hit_rect(x, y, offset_x + ITEM_X, ITEM_Y + i * ITEM_STEP,
		                       ITEM_W, ITEM_H)) continue;
		sidebar->cursor = i;
		return activate(sidebar);
	}
	if (x >= offset_x + SIDEBAR_WIDTH) sidebar->open = 0;
	return UI_SECTION_NONE;
}

void ui_sections_sidebar_tick(UiSectionsSidebar *sidebar) {
	if (!sidebar) return;
	uint64_t now = sceKernelGetProcessTimeWide();
	uint64_t elapsed = sidebar->animation_last_us &&
	                   now > sidebar->animation_last_us
	                 ? now - sidebar->animation_last_us : 16667ULL;
	if (elapsed > 50000ULL) elapsed = 50000ULL;
	sidebar->animation_last_us = now;
	if (vt_preferences_reduce_motion())
		sidebar->animation = sidebar->open ? 1.0f : 0.0f;
	else {
		float alpha = (float)elapsed / (60000.0f + (float)elapsed);
		sidebar->animation += ((sidebar->open ? 1.0f : 0.0f) -
		                       sidebar->animation) * alpha;
	}
	if (!sidebar->open && sidebar->animation < 0.025f) sidebar->animation = 0.0f;
	if (vt_preferences_reduce_motion())
		sidebar->focus_cursor = (float)sidebar->cursor;
	else {
		float alpha = (float)elapsed / (50000.0f + (float)elapsed);
		sidebar->focus_cursor +=
		    ((float)sidebar->cursor - sidebar->focus_cursor) * alpha;
	}
}

void ui_sections_sidebar_draw(int cursor, float animation, float focus_cursor) {
	const char *items[] = {
		vt_i18n_str(VT_STR_SECTIONS_HOME),
		vt_i18n_str(VT_STR_SECTIONS_NETWORK),
		vt_i18n_str(VT_STR_SECTIONS_SETTINGS),
		vt_i18n_str(VT_STR_SECTIONS_ABOUT)
	};
	float ox = -(float)SIDEBAR_WIDTH * (1.0f - animation);
	/* The drawer is a true edge-to-edge layer.  Stopping below the header made
	 * it look detached from the screen and exposed the page below while it was
	 * moving in. */
	vita2d_draw_rectangle(ox, 0, SIDEBAR_WIDTH, SCREEN_HEIGHT,
	                      RGBA8(3, 8, 15, 250));
	vita2d_draw_rectangle(ox + SIDEBAR_WIDTH - 4, 0, 4, SCREEN_HEIGHT,
	                      VT_THEME_BLUE_BRIGHT);
	vita2d_font *font = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	/* The physical-key cap anchors the drawer to Vita navigation and makes the
	 * otherwise quiet full-height top area useful. */
	vita2d_draw_rectangle(ox + 20, 18, 42, 26, VT_THEME_SURFACE_RAISED);
	if (small) {
		int key_width = ui_font_text_width(small, UI_FONT_SMALL, "L1");
		ui_font_draw_text(small, (int)ox + 41 - key_width / 2, 38,
		                  VT_THEME_TEXT, UI_FONT_SMALL, "L1");
		ui_font_draw_text(small, (int)ox + 76, 38, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, "VitaWave");
	}
	if (!font) return;
	ui_font_draw_text(font, (int)ox + 28, 116, VT_THEME_TEXT,
	                      UI_FONT_BODY, vt_i18n_str(VT_STR_SECTIONS_TITLE));
	vita2d_draw_rectangle(ox + ITEM_X, 132, ITEM_W, 1, VT_THEME_BORDER_DIM);
	int has_focus = focus_cursor >= 0.0f;
	if (has_focus) {
		vita2d_draw_rectangle(ox + ITEM_X, ITEM_Y + focus_cursor * ITEM_STEP,
		                      ITEM_W, ITEM_H, VT_THEME_SURFACE_FOCUS);
		vita2d_draw_rectangle(ox + ITEM_X, ITEM_Y + focus_cursor * ITEM_STEP,
		                      4, ITEM_H, VT_THEME_BLUE_LIGHT);
	}
	for (int i = 0; i < UI_SECTION_COUNT; i++) {
		unsigned int color = has_focus && i == cursor ? VT_THEME_BLUE_LIGHT
		                                      : VT_THEME_TEXT_MUTED;
		ui_font_draw_text(font, (int)ox + 34, 183 + i * ITEM_STEP, color,
		                      UI_FONT_BODY, items[i]);
	}
	if (small)
		ui_font_draw_text(small, (int)ox + 28, 516, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_SECTIONS_FOOTER_HINT));
}
