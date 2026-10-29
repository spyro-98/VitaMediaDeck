#ifndef VITAMEDIADECK_UI_SECTIONS_SIDEBAR_H
#define VITAMEDIADECK_UI_SECTIONS_SIDEBAR_H

#include <stdint.h>
#include "ui/focus_glow.h"

enum {
	UI_SECTION_LOCAL_MEDIA = 0,
	UI_SECTION_NETWORK,
	UI_SECTION_ONLINE_AUDIO,
	UI_SECTION_IPTV,
	UI_SECTION_SETTINGS,
	UI_SECTION_GUIDE,
	UI_SECTION_INFO,
	UI_SECTION_NAVIDROME,
	UI_SECTION_IMMICH,
	UI_SECTION_JELLYFIN,
	UI_SECTION_COUNT
};

#define UI_SECTION_NONE (-1)
#define UI_SECTION_PLAYER UI_SECTION_COUNT
#define UI_SECTION_DOWNLOAD (UI_SECTION_COUNT + 1)
#define UI_SECTION_ACTION_COUNT (UI_SECTION_COUNT + 2)
/* Only the audio player enables/consumes these local presentation actions. */
#define UI_SECTION_AUDIO_LAYOUT_BASE (UI_SECTION_ACTION_COUNT)

typedef struct {
	int open;
	int cursor;
	int active;
	int player_available;
	int download_available;
	float animation;
	float focus_cursor;
	UiFocusMotion fixed_focus;
	float scroll, scroll_target, scroll_velocity;
	int scroll_theme;
	int touch_active, touch_dragged, touch_start_y, touch_last_y,scroll_coasting;
    uint64_t touch_last_us,analog_last_us;
	float touch_start_scroll;
	unsigned int repeat_button;
	uint64_t repeat_next_us;
	uint64_t animation_last_us;
	int audio_layout_available, audio_layout_page, audio_layout_cursor, audio_layout_selected;
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

/* UI-thread navigation requested by a resumed audio player. Consumed once. */
void ui_sections_request(int section);
int ui_sections_take_requested(void);

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
void ui_sections_sidebar_draw(const UiSectionsSidebar *sidebar);

#endif /* VITAMEDIADECK_UI_SECTIONS_SIDEBAR_H */
