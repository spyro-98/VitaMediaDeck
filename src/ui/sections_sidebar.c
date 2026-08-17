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
	unsigned int nav = 0;
	if (held_buttons & SCE_CTRL_UP) nav = SCE_CTRL_UP;
	else if (held_buttons & SCE_CTRL_DOWN) nav = SCE_CTRL_DOWN;
	else if (analog_y < 48) nav = SCE_CTRL_UP;
	else if (analog_y > 207) nav = SCE_CTRL_DOWN;
	uint64_t now = sceKernelGetProcessTimeWide();
	unsigned int navigated = *pressed;
	if (!nav) {
		sidebar->repeat_button = 0;
	} else if (nav != sidebar->repeat_button) {
		sidebar->repeat_button = nav;
		sidebar->repeat_next_us = now + 280000ULL;
		navigated |= nav;
	} else if (now >= sidebar->repeat_next_us) {
		sidebar->repeat_next_us = now + 95000ULL;
		navigated |= nav;
	}
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
	for (int i = 0; i < UI_SECTION_COUNT; i++) {
		if (!ui_touch_hit_rect(x, y, ITEM_X, ITEM_Y + i * ITEM_STEP,
		                       ITEM_W, ITEM_H)) continue;
		sidebar->cursor = i;
		return activate(sidebar);
	}
	if (x > SIDEBAR_WIDTH) sidebar->open = 0;
	return UI_SECTION_NONE;
}

void ui_sections_sidebar_tick(UiSectionsSidebar *sidebar) {
	if (!sidebar) return;
	if (vt_preferences_reduce_motion())
		sidebar->animation = sidebar->open ? 1.0f : 0.0f;
	else
		sidebar->animation += ((sidebar->open ? 1.0f : 0.0f) -
		                       sidebar->animation) * 0.28f;
	if (!sidebar->open && sidebar->animation < 0.01f) sidebar->animation = 0.0f;
	if (vt_preferences_reduce_motion())
		sidebar->focus_cursor = (float)sidebar->cursor;
	else
		sidebar->focus_cursor += ((float)sidebar->cursor - sidebar->focus_cursor) * 0.32f;
}

void ui_sections_sidebar_draw(int cursor, float animation, float focus_cursor) {
	const char *items[] = {
		"Home",
		"Network sources",
		"Settings",
		"About"
	};
	float ox = -(float)SIDEBAR_WIDTH * (1.0f - animation);
	vita2d_draw_rectangle(ox, UI_BRAND_HEADER_HEIGHT, SIDEBAR_WIDTH,
	                      SCREEN_HEIGHT - UI_BRAND_HEADER_HEIGHT,
	                      RGBA8(3, 8, 15, 250));
	vita2d_draw_rectangle(ox + SIDEBAR_WIDTH - 4, UI_BRAND_HEADER_HEIGHT, 4,
	                      SCREEN_HEIGHT - UI_BRAND_HEADER_HEIGHT,
	                      VT_THEME_BLUE_BRIGHT);
	vita2d_font *font = ui_runtime_font(UI_FONT_BODY);
	if (!font) return;
	ui_font_draw_text(font, (int)ox + 28, 116, VT_THEME_TEXT,
	                      UI_FONT_BODY, "Sections");
	vita2d_draw_rectangle(ox + ITEM_X, ITEM_Y + focus_cursor * ITEM_STEP,
	                      ITEM_W, ITEM_H, VT_THEME_SURFACE_FOCUS);
	for (int i = 0; i < UI_SECTION_COUNT; i++) {
		unsigned int color = i == cursor ? VT_THEME_BLUE_LIGHT
		                                      : VT_THEME_TEXT_MUTED;
		ui_font_draw_text(font, (int)ox + 34, 183 + i * ITEM_STEP, color,
		                      UI_FONT_BODY, items[i]);
	}
}
