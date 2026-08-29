#include "ui/components.h"

#include <math.h>
#include <stdint.h>

#include <psp2/kernel/processmgr.h>

#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/runtime.h"
#include "ui/theme.h"

static unsigned int channel(unsigned int color, unsigned int shift) {
	return (color >> shift) & 0xffU;
}

unsigned int ui_contrast_bw(unsigned int background) {
	unsigned int red = channel(background, 0);
	unsigned int green = channel(background, 8);
	unsigned int blue = channel(background, 16);
	/* Integer approximation of relative luminance; 142 is a slightly
	 * conservative threshold for small system-PGF text on the Vita OLED. */
	unsigned int luminance = (red * 299U + green * 587U + blue * 114U) / 1000U;
	return luminance >= 142U ? RGBA8(0, 0, 0, 255)
	                         : RGBA8(255, 255, 255, 255);
}

static void draw_particle_cloud(float cx, float cy, float width, float height,
	                            uint32_t salt, uint64_t now,
	                            int reduced_motion, int warm_core) {
	float drift_x = 0.0f, drift_y = 0.0f;
	if (!reduced_motion) {
		float phase = (float)((now / 1000ULL + salt) % 16000ULL) / 16000.0f;
		drift_x = sinf(phase * 6.2831853f) * 18.0f;
		drift_y = cosf(phase * 6.2831853f * 0.73f) * 9.0f;
	}
	cx += drift_x;
	cy += drift_y;
	/* Overlapping translucent cells create a real volumetric wisp rather than a
	 * flat constellation. The deterministic field needs no allocation or RNG. */
	for (unsigned int i = 0; i < 30U; i++) {
		uint32_t seed = (salt + i * 0x9E3779B9U) * 0x45D9F3BU;
		seed ^= seed >> 16;
		float nx = ((float)((seed >> 5) % 1000U) / 500.0f) - 1.0f;
		float ny = ((float)((seed >> 17) % 1000U) / 500.0f) - 1.0f;
		float falloff = 1.0f - (nx * nx + ny * ny) * 0.28f;
		if (falloff < 0.18f) falloff = 0.18f;
		float px = cx + nx * width * 0.5f;
		float py = cy + ny * height * 0.5f;
		float radius = 3.0f + (float)(seed % 10U);
		unsigned int alpha = (unsigned int)((5U + seed % 13U) * falloff);
		unsigned int color = warm_core && i % 7U == 0U
		                   ? VT_THEME_WARM_A(alpha + 3U)
		                   : i % 4U == 0U ? VT_THEME_SPECTRAL_A(alpha)
		                                     : VT_THEME_COLD_A(alpha);
		vita2d_draw_fill_circle(px, py, radius, color);
		if (i % 3U == 0U)
			vita2d_draw_rectangle(px, py, i % 9U == 0U ? 2.0f : 1.0f,
			                      i % 9U == 0U ? 2.0f : 1.0f,
			                      i % 7U == 0U ? VT_THEME_SIGNAL_A(92)
			                                    : VT_THEME_SPECTRAL_A(74));
	}
}

void ui_chrome_background(unsigned int base, unsigned int accent) {
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT, base);
	/* Faint submerged strata add depth without lighting otherwise-black OLED
	 * pixels across a large area. */
	for (int line = 0; line < 7; line++) {
		float y = 92.0f + line * 66.0f;
		vita2d_draw_rectangle(0, y, 960, 1,
		                      line == 0 ? VT_THEME_COLD_A(42)
		                                : VT_THEME_COLD_A(12));
	}
	vita2d_draw_rectangle(684, UI_BRAND_HEADER_HEIGHT, 1,
	                      544 - UI_BRAND_HEADER_HEIGHT,
	                      VT_THEME_SIGNAL_A(22));

	/* A deterministic, low-cost signal field. Motion is a slow vertical phase,
	 * disabled by Reduce motion; no random state or allocation touches frames. */
	uint64_t now = sceKernelGetProcessTimeWide();
	int reduced_motion = vt_preferences_reduce_motion();
	unsigned int phase = reduced_motion
	                   ? 0U : (unsigned int)((now / 42000ULL) % 420ULL);
	for (unsigned int i = 0; i < 82; i++) {
		uint32_t seed = 0x9E3779B9U * (i + 11U);
		seed ^= seed >> 16;
		float x = 18.0f + (float)(seed % 924U);
		float y = 76.0f + (float)((seed / 331U + phase * (1U + i % 3U)) % 448U);
		float size = i % 11U == 0U ? 3.0f : i % 4U == 0U ? 2.0f : 1.0f;
		unsigned int alpha = 18U + (seed % 62U);
		unsigned int accent_particle =
		    (accent & 0x00ffffffU) | ((alpha + 18U) << 24);
		unsigned int color = i % 37U == 0U ? VT_THEME_WARM_A(alpha + 12U)
		                   : i % 7U == 0U ? accent_particle
		                   : i % 3U == 0U ? VT_THEME_COLD_A(alpha)
		                                    : VT_THEME_SPECTRAL_A(alpha);
		vita2d_draw_rectangle(x, y, size, size, color);
		if (i % 13U == 0U)
			vita2d_draw_line(x, y, 842.0f, 296.0f,
			                 i % 26U == 0U ? VT_THEME_SIGNAL_A(18)
			                                : VT_THEME_SPECTRAL_A(13));
	}
	draw_particle_cloud(744.0f, 228.0f, 248.0f, 116.0f,
	                    0xA61C3U, now, reduced_motion, 0);
	draw_particle_cloud(844.0f, 374.0f, 186.0f, 154.0f,
	                    0x71B29U, now, reduced_motion, 1);
	draw_particle_cloud(278.0f, 456.0f, 270.0f, 72.0f,
	                    0xC04D7U, now, reduced_motion, 0);
	/* A cold memory scan crosses only the machine-side telemetry field and
	 * freezes to a quiet datum when Reduce motion is enabled. */
	unsigned int scan_phase = reduced_motion
	                        ? 132U : (unsigned int)((now / 26000ULL) % 264ULL);
	float scan_x = 688.0f + (float)scan_phase;
	vita2d_draw_rectangle(scan_x - 7.0f, 118.0f, 1.0f, 410.0f,
	                      VT_THEME_COLD_A(28));
	vita2d_draw_rectangle(scan_x - 2.0f, 118.0f, 1.0f, 410.0f,
	                      VT_THEME_COLD_A(76));
	vita2d_draw_rectangle(scan_x, 118.0f, 2.0f, 410.0f,
	                      VT_THEME_COLD_A(150));
	for (unsigned int packet = 0; packet < 12; packet++) {
		uint32_t seed = 0x45D9F3BU * (packet + 19U);
		seed ^= seed >> 15;
		float packet_y = 126.0f + (float)(seed % 392U);
		float trail = 7.0f + (float)(seed % 21U);
		vita2d_draw_rectangle(scan_x - trail, packet_y, trail, 1.0f,
		                      VT_THEME_COLD_A(44U + seed % 68U));
		vita2d_draw_rectangle(scan_x + 3.0f, packet_y - 1.0f,
		                      packet % 4U == 0U ? 5.0f : 2.0f, 2.0f,
		                      VT_THEME_COLD_A(118U));
	}
	/* The shell seam remains a quiet structural anchor beneath the drifting
	 * cloud. Warm copper appears as a reflected trace, not a dominant panel. */
	vita2d_draw_line(704.0f, 492.0f, 932.0f, 142.0f,
	                 VT_THEME_SPECTRAL_A(13));
	vita2d_draw_line(720.0f, 506.0f, 944.0f, 170.0f,
	                 VT_THEME_WARM_A(10));
	for (int ring = 2; ring >= 0; ring--) {
		float radius = 54.0f + ring * 35.0f;
		unsigned int edge = ring == 2 ? VT_THEME_COLD_A(20)
		                  : ring == 1 ? VT_THEME_SPECTRAL_A(24)
		                              : VT_THEME_WARM_A(16);
		vita2d_draw_fill_circle(842.0f, 296.0f, radius, edge);
		vita2d_draw_fill_circle(842.0f, 296.0f, radius - 2.0f,
		                        (base & 0x00ffffffU) | (244U << 24));
	}
	vita2d_draw_rectangle(0, 116, 960, 2, VT_THEME_BORDER_DIM);
}

void ui_scene_identity(float x, float y, float width, const char *code,
	                   const char *title, const char *detail) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	const float code_width = 62.0f;
	vita2d_draw_rectangle(x, y, 3.0f, 46.0f, VT_THEME_SIGNAL);
	vita2d_draw_rectangle(x + 3.0f, y, code_width, 1.0f,
	                      VT_THEME_SIGNAL_BRIGHT);
	vita2d_draw_rectangle(x + 3.0f, y + 45.0f, width - 3.0f, 1.0f,
	                      VT_THEME_BORDER_DIM);
	if (small && code && code[0])
		ui_font_draw_text(small, (int)x + 12, (int)y + 27,
		                  VT_THEME_SIGNAL_LIGHT, UI_FONT_SMALL, code);
	if (body && title && title[0]) {
		char fitted[192];
		ui_font_fit_text(body, UI_FONT_BODY, title, fitted, sizeof(fitted),
		                 (int)(width - code_width - 14.0f));
		ui_font_draw_text(body, (int)(x + code_width + 10.0f), (int)y + 22,
		                  VT_THEME_TEXT, UI_FONT_BODY, fitted);
	}
	if (small && detail && detail[0]) {
		char fitted[256];
		ui_font_fit_text(small, UI_FONT_SMALL, detail, fitted, sizeof(fitted),
		                 (int)(width - code_width - 14.0f));
		ui_font_draw_text(small, (int)(x + code_width + 10.0f), (int)y + 42,
		                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
	}
}

static void panel_corner(float x, float y, float sx, float sy,
	                     unsigned int color) {
	float horizontal_x = sx > 0.0f ? x : x - 14.0f;
	float horizontal_y = sy > 0.0f ? y : y - 2.0f;
	float vertical_x = sx > 0.0f ? x : x - 2.0f;
	float vertical_y = sy > 0.0f ? y : y - 11.0f;
	vita2d_draw_rectangle(horizontal_x, horizontal_y, 14.0f, 2.0f, color);
	vita2d_draw_rectangle(vertical_x, vertical_y, 2.0f, 11.0f, color);
}

void ui_panel(float x, float y, float width, float height,
	          unsigned int fill, unsigned int accent, int focused) {
	if (focused)
		vita2d_draw_rectangle(x - 2, y - 2, width + 4, height + 4,
		                      VT_THEME_HALO_A(54));
	vita2d_draw_rectangle(x, y, width, height, fill);
	vita2d_draw_rectangle(x, y, width, 1,
	                      focused ? accent : VT_THEME_BORDER_DIM);
	vita2d_draw_rectangle(x, y + height - 1, width, 1,
	                      VT_THEME_COLD_A(82));
	unsigned int mark = focused ? VT_THEME_SIGNAL_LIGHT : accent;
	panel_corner(x, y, 1.0f, 1.0f, mark);
	panel_corner(x + width, y, -1.0f, 1.0f, mark);
	panel_corner(x, y + height, 1.0f, -1.0f, mark);
	panel_corner(x + width, y + height, -1.0f, -1.0f, mark);
}

void ui_action_button(float x, float y, float width, float height,
	                  unsigned int fill, const char *key, const char *label,
	                  int active) {
	unsigned int foreground = ui_contrast_bw(fill);
	ui_panel(x, y, width, height, fill,
	         active ? VT_THEME_SIGNAL_LIGHT : VT_THEME_BORDER, active);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	float key_width = 0.0f;
	if (small && key && key[0]) {
		key_width = (float)ui_font_text_width(small, UI_FONT_SMALL, key) + 14.0f;
		if (key_width < 34.0f) key_width = 34.0f;
	}
	if (key_width > 0.0f) {
		vita2d_draw_rectangle(x + 10, y + 9, key_width, height - 18,
		                      active ? VT_THEME_SPECTRAL_A(42)
		                             : RGBA8(255, 255, 255, 18));
		if (small) {
			int kw = ui_font_text_width(small, UI_FONT_SMALL, key);
			ui_font_draw_text(small, (int)(x + 10 + (key_width - kw) * .5f),
			                  (int)(y + height * .5f + 7), foreground,
			                  UI_FONT_SMALL, key);
		}
	}
	if (small && label) {
		char fitted[192];
		float left = x + key_width + 17.0f;
		float available = width - key_width - 24.0f;
		ui_font_fit_text(small, UI_FONT_SMALL, label, fitted, sizeof(fitted),
		                 (int)available);
		int label_width = ui_font_text_width(small, UI_FONT_SMALL, fitted);
		ui_font_draw_text(small, (int)(left + (available - label_width) * .5f),
		                  (int)(y + height * .5f + 7), foreground,
		                  UI_FONT_SMALL, fitted);
	}
}
