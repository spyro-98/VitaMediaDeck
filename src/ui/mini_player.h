#ifndef VITATUBE_UI_MINI_PLAYER_H
#define VITATUBE_UI_MINI_PLAYER_H

#include "ui/touch.h"

#define UI_MINI_PLAYER_Y 478
#define UI_MINI_PLAYER_HEIGHT 66

/* Completes any pending fetch and creates/frees the texture on the UI
 * thread, outside an already-open vita2d scene. */
void ui_mini_player_pump(void);

/* Returns whether the bottom bar currently occupies screen space. */
int ui_mini_player_visible(void);
/* Animated top edge in screen pixels (544 when fully hidden). Pages use this
 * instead of snapping their viewport to UI_MINI_PLAYER_Y at animation start. */
int ui_mini_player_top(void);
int ui_mini_player_input_locked(void);

/* START is reserved globally for mini-player mode. When a fullscreen resume
 * handler is installed it reopens that player; it never doubles as stop.
 * SELECT held for 900 ms locks/unlocks every mini-player and page input. */
int ui_mini_player_handle_buttons(unsigned int *pressed);

/* Draws into the already-open vita2d frame. No-op when the service has not
 * been activated by the player screen. */
void ui_mini_player_draw(void);

/* Handles only the bottom bar and returns 1 when the event must not reach
 * the screen underneath. */
int ui_mini_player_handle_touch(unsigned int touch_flags,
	                            const UiTouchEvent *touch);

/* Joins any pending cover fetch before closing network/GXM. */
void ui_mini_player_shutdown(void);

#endif /* VITATUBE_UI_MINI_PLAYER_H */
