#include "ui/components.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <psp2/kernel/processmgr.h>

#include "settings/preferences.h"
#include "history/playback_history.h"
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

/* Background detail lives outside the media's 66..894 content column.
 * Static primitives avoid continuous scans behind text and require neither
 * intermediate render targets nor per-frame particle/circle tessellation. */
void ui_chrome_background(unsigned int base, unsigned int accent) {
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT, base);
	unsigned int seam = (accent & 0x00ffffffU) | (48U << 24);
	vita2d_draw_rectangle(24, 94, 1, 416, VT_THEME_BORDER_DIM);
	vita2d_draw_rectangle(935, 94, 1, 416, VT_THEME_BORDER_DIM);
	for (int i = 0; i < 8; i++) {
		float y = 110.0f + i * 52.0f;
		float length = i % 3 == 0 ? 12.0f : 6.0f;
		vita2d_draw_rectangle(24, y, length, 1, seam);
		vita2d_draw_rectangle(935 - length, y, length, 1, seam);
	}
	/* One asymmetric signal terminal identifies the shell, not a busy state. */
	vita2d_draw_rectangle(24, 94, 3, 22, VT_THEME_SIGNAL_A(130));
	vita2d_draw_rectangle(918, 509, 18, 2, VT_THEME_SIGNAL_A(80));
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
	/* Focus is structural, not four brackets competing with nearby text. */
	if (focused) {
		vita2d_draw_rectangle(x, y, 3, height, VT_THEME_SIGNAL_LIGHT);
		vita2d_draw_rectangle(x + width - 1, y, 1, height, VT_THEME_SIGNAL);
	} else {
		vita2d_draw_rectangle(x, y, 2, height < 18 ? height : 18,
		                      (accent & 0x00ffffffU) | (120U << 24));
	}
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
		ui_font_draw_text(small, (int)left,
		                  (int)(y + height * .5f + 7), foreground,
		                  UI_FONT_SMALL, fitted);
	}
}

void ui_watched_progress(const char *history_id, float x, float y, float width) {
	uint64_t position_ms = 0, duration_ms = 0;
	if (!history_id || width < 24.0f ||
	    !vt_playback_history_progress(history_id, &position_ms, &duration_ms) ||
	    !duration_ms) return;
	if (position_ms > duration_ms) position_ms = duration_ms;
	float fraction = (float)((double)position_ms / (double)duration_ms);
	unsigned int percent = (unsigned int)((position_ms * 100ULL +
	                                      duration_ms / 2ULL) / duration_ms);
	if (percent > 100U) percent = 100U;
	vita2d_draw_rectangle(x, y, width, 6, RGBA8(0, 0, 0, 224));
	vita2d_draw_rectangle(x + 1, y + 1, (width - 2) * fraction, 4,
	                      percent >= 100U ? VT_THEME_SIGNAL_LIGHT
	                                      : VT_THEME_SPECTRAL);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (small) {
		char label[8];
		snprintf(label, sizeof(label), "%u%%", percent);
		int label_width = ui_font_text_width(small, UI_FONT_SMALL, label);
		float pill_width = label_width + 10.0f;
		vita2d_draw_rectangle(x + width - pill_width, y - 19, pill_width, 18,
		                      RGBA8(0, 0, 0, 210));
		ui_font_draw_text(small, (int)(x + width - label_width - 5), (int)y - 5,
		                  VT_THEME_TEXT, UI_FONT_SMALL, label);
	}
}
