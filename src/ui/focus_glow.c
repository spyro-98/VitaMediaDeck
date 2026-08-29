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
	/* A calmer 250 ms settle makes grid motion readable without delaying input. */
	float blend = 1.0f - expf(-12.0f * delta_seconds);
	motion->x += (x - motion->x) * blend;
	motion->y += (y - motion->y) * blend;
	motion->width += (width - motion->width) * blend;
	motion->height += (height - motion->height) * blend;
}

static unsigned int halo_color(int layer, int alpha) {
	if (alpha < 0) alpha = 0;
	if (alpha > 255) alpha = 255;
	if (layer == 0) return RGBA8(18, 57, 63, alpha);
	if (layer == 1) return RGBA8(150, 174, 171, alpha);
	return RGBA8(226, 128, 41, alpha);
}

void ui_focus_glow_draw(float x, float y, float width, float height,
	                    uint64_t now_us, int viewport_top,
	                    int viewport_bottom) {
	if (width <= 0.0f || height <= 0.0f || viewport_bottom <= viewport_top)
		return;
	int reduced_motion = vt_preferences_reduce_motion();
	float pulse = 0.72f;
	if (!reduced_motion) {
		float phase = (float)((now_us / 1000ULL) % 5200ULL) / 5200.0f;
		pulse = 0.78f + 0.12f * (0.5f + 0.5f * sinf(phase * 6.2831853f));
	}

	int clip_left = (int)x - 24;
	int clip_right = (int)(x + width) + 24;
	if (clip_left < 0) clip_left = 0;
	if (clip_right > 960) clip_right = 960;
	vita2d_set_clip_rectangle(clip_left, viewport_top, clip_right,
	                          viewport_bottom);
	vita2d_enable_clipping();

	/* The bloom sits behind content; cards cover its centre and leave a compact
	 * spectral reassembly field around the focused object. */
	float cx = x + width * 0.5f;
	float cy = y + height * 0.5f;
	float radius = height * 0.5f + 19.0f;
	for (int layer = 0; layer < 3; layer++) {
		float r = radius - (float)layer * 8.0f;
		int alpha = (int)((10 + layer * 9) * pulse);
		vita2d_draw_fill_circle(cx, cy, r, VT_THEME_HALO_A(alpha));
	}

	static const int spread[3] = { 12, 7, 3 };
	static const int base_alpha[3] = { 22, 42, 82 };
	for (int layer = 0; layer < 3; layer++) {
		int s = spread[layer];
		vita2d_draw_rectangle(x - s, y - s, width + s * 2.0f,
		                      height + s * 2.0f,
		                      halo_color(layer,
		                          (int)(base_alpha[layer] * pulse)));
	}

	/* Thin signal rails preserve focus legibility over both dark and bright art. */
	unsigned int rail = VT_THEME_SIGNAL_A((int)(248.0f * pulse));
	vita2d_draw_rectangle(x - 4.0f, y - 4.0f, width + 8.0f, 3.0f, rail);
	vita2d_draw_rectangle(x - 4.0f, y + height + 1.0f, width + 8.0f, 3.0f, rail);
	vita2d_draw_rectangle(x - 4.0f, y - 4.0f, 2.0f, 16.0f, rail);
	vita2d_draw_rectangle(x + width + 2.0f, y + height - 12.0f,
	                      2.0f, 16.0f, rail);
	/* Cold centre ticks read as machine acquisition; the warm rails continue to
	 * identify the user's focus. */
	vita2d_draw_rectangle(x + width * 0.5f - 10.0f, y - 7.0f,
	                      20.0f, 2.0f, VT_THEME_COLD_A((int)(176.0f * pulse)));
	vita2d_draw_rectangle(x + width * 0.5f - 10.0f, y + height + 5.0f,
	                      20.0f, 2.0f, VT_THEME_COLD_A((int)(112.0f * pulse)));
	/* Fine deterministic fragments materialize around all four edges. Motion is
	 * a short local drift rather than a continuous neon pulse, and freezes when
	 * Reduce motion is enabled. */
	unsigned int phase = reduced_motion
	                   ? 0U : (unsigned int)((now_us / 150000ULL) % 97ULL);
	for (unsigned int dot = 0; dot < 18U; dot++) {
		uint32_t seed = 0x9E3779B9U * (dot + 23U);
		seed ^= seed >> 16;
		float along = (float)((seed >> 8) % 1000U) / 1000.0f;
		float offset = 6.0f + (float)(seed % 15U);
		float drift = reduced_motion ? 0.0f
		            : (float)((phase * (1U + dot % 3U) + seed) % 11U) - 5.0f;
		float px, py;
		switch (seed & 3U) {
			case 0: px = x + along * width + drift; py = y - offset; break;
			case 1: px = x + width + offset; py = y + along * height + drift; break;
			case 2: px = x + along * width - drift; py = y + height + offset; break;
			default: px = x - offset; py = y + along * height - drift; break;
		}
		unsigned int alpha = 86U + seed % 104U;
		unsigned int color = dot % 6U == 0U ? VT_THEME_SIGNAL_A(alpha)
		                   : dot % 4U == 0U ? VT_THEME_COLD_A(alpha)
		                                     : VT_THEME_SPECTRAL_A(alpha);
		float size = dot % 7U == 0U ? 3.0f : dot % 3U == 0U ? 2.0f : 1.0f;
		vita2d_draw_rectangle(px, py, size, size, color);
		if (dot % 8U == 0U)
			vita2d_draw_line(px, py, cx, cy, VT_THEME_SPECTRAL_A(18));
	}
	vita2d_disable_clipping();
}
