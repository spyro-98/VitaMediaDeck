#ifndef VITATUBE_UI_SECTIONS_SIDEBAR_H
#define VITATUBE_UI_SECTIONS_SIDEBAR_H

#include <stdint.h>

enum {
	UI_SECTION_LOCAL_MEDIA = 0,
	UI_SECTION_NETWORK,
	UI_SECTION_SETTINGS,
	UI_SECTION_INFO,
	UI_SECTION_COUNT
};

#define UI_SECTION_NONE (-1)

typedef struct {
	int open;
	int cursor;
	int active;
	float animation;
	float focus_cursor;
	unsigned int repeat_button;
	uint64_t repeat_next_us;
	uint64_t animation_last_us;
} UiSectionsSidebar;

/* Shared directional repeat for screens that accept both the d-pad and the
 * left stick. Keeping the thresholds and cadence in one place prevents the
 * stick from feeling different between grids, lists and modal menus. */
typedef struct {
	unsigned int direction;
	uint64_t repeat_next_us;
} UiNavRepeat;

void ui_nav_repeat_reset(UiNavRepeat *repeat);
unsigned int ui_nav_repeat_update(UiNavRepeat *repeat,
	                              unsigned int pressed,
	                              unsigned int held_buttons,
	                              unsigned char analog_x,
	                              unsigned char analog_y,
	                              unsigned int allowed_directions);

void ui_sections_sidebar_init(UiSectionsSidebar *sidebar, int active);

/* L1 is consumed exclusively as an open/close toggle. While open, the
 * sidebar consumes its own navigation buttons. Returns the selected section
 * or UI_SECTION_NONE; selecting the already active section only closes it. */
int ui_sections_sidebar_handle_buttons(UiSectionsSidebar *sidebar,
	                                   unsigned int *pressed,
	                                   unsigned int held_buttons,
	                                   unsigned char analog_y);
int ui_sections_sidebar_handle_touch(UiSectionsSidebar *sidebar,
	                                 unsigned int touch_flags, int x, int y);
void ui_sections_sidebar_tick(UiSectionsSidebar *sidebar);
void ui_sections_sidebar_draw(int cursor, float animation, float focus_cursor);

#endif /* VITATUBE_UI_SECTIONS_SIDEBAR_H */
