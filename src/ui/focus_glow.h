#ifndef VITAMEDIADECK_UI_FOCUS_GLOW_H
#define VITAMEDIADECK_UI_FOCUS_GLOW_H

#include <stdint.h>

/* A single selector is used as a physical cursor above list rows, poster
 * cards and tabs.  Keeping its state separate from the selected item's state
 * lets the cards remain cheap to draw while focus visibly travels between
 * them. */
typedef struct {
	float x;
	float y;
	float width;
	float height;
	uint64_t last_tick_us;
	int initialized;
} UiFocusMotion;

void ui_focus_motion_reset(UiFocusMotion *motion);
void ui_focus_motion_tick(UiFocusMotion *motion, float x, float y,
	                      float width, float height);

/* Drawn before the poster/card: only the oversized amber halo remains visible
 * around its edges, so the selector is physically behind it on the Z axis. */
void ui_focus_glow_draw(float x, float y, float width, float height,
	                    uint64_t now_us, int viewport_top,
	                    int viewport_bottom);

#endif /* VITAMEDIADECK_UI_FOCUS_GLOW_H */
