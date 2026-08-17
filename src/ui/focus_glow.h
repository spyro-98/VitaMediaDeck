#ifndef VITATUBE_UI_FOCUS_GLOW_H
#define VITATUBE_UI_FOCUS_GLOW_H

#include <stdint.h>

/* Drawn before the poster/card: only the oversized blue halo remains visible
 * around its edges, so the selector is physically behind it on the Z axis. */
void ui_focus_glow_draw(float x, float y, float width, float height,
	                    uint64_t now_us, int viewport_top,
	                    int viewport_bottom);

#endif /* VITATUBE_UI_FOCUS_GLOW_H */
