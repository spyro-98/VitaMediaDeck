#ifndef VITAMEDIADECK_UI_COMPONENTS_H
#define VITAMEDIADECK_UI_COMPONENTS_H

#include <vita2d.h>
#include "ui/sections_sidebar.h"
#include "ui/touch.h"
#include "ui/focus_glow.h"
#include "ui/scroll_view.h"
#include "ui/theme.h"
#include "ui/menu_geometry.h"

/* Menus inherit the active palette, including the light Signal/media override.
 * Only theme 7 owns the blue PAF material. */
#define UI_MENU_BG VT_THEME_BG_SOFT
#define UI_MENU_TEXT VT_THEME_TEXT
#define UI_MENU_MUTED VT_THEME_TEXT_MUTED
#define UI_MENU_FAINT VT_THEME_TEXT_FAINT
#define UI_MENU_ACCENT VT_THEME_SIGNAL_BRIGHT

typedef struct {
	int open, cursor, top, touch_start_cursor;
	float animation;
	uint64_t last_us;
	UiNavRepeat repeat;
	UiFocusMotion focus;
    UiScrollView viewport;
    unsigned group_starts;
} UiContextMenu;
typedef struct {
	const char *label, *detail;
	int action, enabled;
} UiContextAction;
enum { UI_MENU_GROUP_PLAYBACK=1, UI_MENU_GROUP_LIBRARY, UI_MENU_GROUP_VIEW,
       UI_MENU_GROUP_CONNECTION, UI_MENU_GROUP_MAINTENANCE, UI_MENU_GROUP_INFO };
/* Group metadata occupies high bits; bit zero alone controls availability. */
void ui_context_menu_category(UiContextAction *items,int count,int action,int group);
const char *ui_menu_group_label(int group);
void ui_menu_category(float x,float y,float width,int group);
/* R1 options share one input/animation contract. L1 is left unconsumed so the
 * caller can switch directly to Sections. Returns the chosen action or -1. */
int ui_context_menu_input(UiContextMenu *menu, const UiContextAction *items, int count,
	unsigned *pressed, unsigned held, unsigned char ly, unsigned *touches, const UiTouchEvent *touch);
void ui_context_menu_tick(UiContextMenu *menu);
void ui_context_menu_draw(const UiContextMenu *menu, const char *title,
	const UiContextAction *items, int count);
void ui_folder_icon(float x, float y);
void ui_artwork_tile(float x, float y, float size, vita2d_texture *texture, const char *placeholder);
void ui_scroll_rail(float x, float y, float height, int first, int visible, int count);
/* Crossfade page primitives. Active context is never drawn as input focus. */
void ui_tab(float x, float y, float width, float height, const char *label, int active);
void ui_text_line(float x, float baseline, float width, const char *text, unsigned color, unsigned size);
void ui_text_block(float x, float baseline, float width, int line_height, int lines, const char *text, unsigned color, unsigned size);
void ui_footer(int bottom, const char *left, const char *right);
void ui_modal_panel(float x, float y, float width, float height);

/* Returns opaque black or white using perceived luminance. */
unsigned int ui_contrast_bw(unsigned int background);

/* Structural signal field shared by library and source-management screens. */
void ui_chrome_background(unsigned int base, unsigned int accent);

/* Real title and translated description in the command band. The historical
 * code argument is retained for call compatibility but no longer displayed. */
void ui_scene_identity(float x, float y, float width, const char *code,
	                   const char *title, const char *detail);

void ui_panel(float x, float y, float width, float height,
	          unsigned int fill, unsigned int accent, int focused);

/* Draws a controller action with a reserved icon/key slot and a fitted verb.
 * Downloaded pad icons keep their colours; text contrasts with the fill. */
void ui_menu_focus_draw(const UiFocusMotion *focus);
void ui_menu_text(float x,float baseline,float width,const char *text,unsigned color,unsigned size);
void ui_menu_surface(float x,float y,float width,float height,float opacity);
void ui_menu_row(float x,float y,float width,float height,int focused,float opacity);
void ui_action_button(float x, float y, float width, float height,
	                  unsigned int fill, const char *key, const char *label,
	                  int active);

/* Draws a compact watched-position rail and exact percentage over a video
 * thumbnail. No output is produced when the item has no history. */
void ui_watched_progress(const char *history_id, float x, float y, float width);

#endif /* VITAMEDIADECK_UI_COMPONENTS_H */
