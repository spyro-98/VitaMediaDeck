#include "ui/focus_glow.h"

#include <math.h>

#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "settings/preferences.h"
#include "ui/theme.h"

void ui_focus_motion_reset(UiFocusMotion *motion) {
	if (!motion) return;
	motion->x = motion->y = motion->width = motion->height = 0.0f;
	motion->last_tick_us = 0;
	motion->initialized = 0;
}

void ui_focus_motion_tick(UiFocusMotion *motion, float x, float y,
	                      float width, float height) {
	if (!motion) return;
	uint64_t now = sceKernelGetProcessTimeWide();
	if (!motion->initialized || vt_preferences_reduce_motion()) {
		motion->x = x;
		motion->y = y;
		motion->width = width;
		motion->height = height;
		motion->last_tick_us = now;
		motion->initialized = 1;
		return;
	}
	float delta_seconds = motion->last_tick_us
	                    ? (float)(now - motion->last_tick_us) / 1000000.0f
	                    : 1.0f / 60.0f;
	motion->last_tick_us = now;
	if (delta_seconds < 0.0f) delta_seconds = 0.0f;
	if (delta_seconds > 0.05f) delta_seconds = 0.05f;
	/* Roughly 165 ms to settle at 95%, independent of frame pacing: quick
	 * enough for repeated D-pad/stick input, but visibly continuous. */
	float blend = 1.0f - expf(-18.0f * delta_seconds);
	motion->x += (x - motion->x) * blend;
	motion->y += (y - motion->y) * blend;
	motion->width += (width - motion->width) * blend;
	motion->height += (height - motion->height) * blend;
}

static unsigned int halo_color(int layer, int alpha) {
	if (alpha < 0) alpha = 0;
	if (alpha > 255) alpha = 255;
	if (layer == 0) return RGBA8(7, 48, 82, alpha);
	if (layer == 1) return RGBA8(18, 88, 139, alpha);
	return RGBA8(42, 143, 212, alpha);
}

void ui_focus_glow_draw(float x, float y, float width, float height,
	                    uint64_t now_us, int viewport_top,
	                    int viewport_bottom) {
	if (width <= 0.0f || height <= 0.0f || viewport_bottom <= viewport_top)
		return;
	float pulse = 0.72f;
	if (!vt_preferences_reduce_motion()) {
		float phase = (float)((now_us / 1000ULL) % 2600ULL) / 2600.0f;
		pulse = 0.68f + 0.20f * (0.5f + 0.5f * sinf(phase * 6.2831853f));
	}

	int clip_left = (int)x - 24;
	int clip_right = (int)(x + width) + 24;
	if (clip_left < 0) clip_left = 0;
	if (clip_right > 960) clip_right = 960;
	vita2d_set_clip_rectangle(clip_left, viewport_top, clip_right,
	                          viewport_bottom);
	vita2d_enable_clipping();

	/* Wide central bloom from the icon. It is intentionally drawn first and
	 * largely covered by the poster that follows, leaving light only outside. */
	float cx = x + width * 0.5f;
	float cy = y + height * 0.5f;
	float radius = height * 0.5f + 27.0f;
	for (int layer = 0; layer < 4; layer++) {
		float r = radius - (float)layer * 7.0f;
		int alpha = (int)((12 + layer * 7) * pulse);
		vita2d_draw_fill_circle(cx, cy, r, VT_THEME_HALO_A(alpha));
	}

	/* Three oversized plates make the light hug every poster edge. */
	static const int spread[3] = { 16, 10, 5 };
	static const int base_alpha[3] = { 24, 39, 76 };
	for (int layer = 0; layer < 3; layer++) {
		int s = spread[layer];
		vita2d_draw_rectangle(x - s, y - s, width + s * 2.0f,
		                      height + s * 2.0f,
		                      halo_color(layer,
		                          (int)(base_alpha[layer] * pulse)));
	}

	/* This thin plate is still behind the poster; its 3 px oversize reads as a
	 * crisp blue rim without ever painting over thumbnail pixels. */
	vita2d_draw_rectangle(x - 3.0f, y - 3.0f, width + 6.0f, height + 6.0f,
	                      VT_THEME_BLUE_A((int)(205.0f * pulse)));
	vita2d_disable_clipping();
}
