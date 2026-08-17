#include "ui/components.h"

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

void ui_chrome_background(unsigned int base, unsigned int accent) {
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT, base);
	/* Offset rings suggest a media signal propagating through the library.
	 * They are structural enough to enrich empty space, but remain behind text. */
	for (int ring = 6; ring >= 1; ring--) {
		unsigned int alpha = 5U + (unsigned int)(7 - ring) * 3U;
		unsigned int color = (accent & 0x00ffffffU) | (alpha << 24);
		vita2d_draw_fill_circle(846.0f, 126.0f, 70.0f + ring * 38.0f, color);
	}
	vita2d_draw_rectangle(0, 118, 960, 1, VT_THEME_BORDER_DIM);
}

void ui_panel(float x, float y, float width, float height,
	          unsigned int fill, unsigned int accent, int focused) {
	if (focused) {
		vita2d_draw_rectangle(x - 3, y - 3, width + 6, height + 6,
		                      VT_THEME_HALO_A(88));
	}
	vita2d_draw_rectangle(x, y, width, height, fill);
	vita2d_draw_rectangle(x, y, 4, height, accent);
	vita2d_draw_rectangle(x + 4, y, width - 4, 1,
	                      focused ? accent : VT_THEME_BORDER_DIM);
}

void ui_action_button(float x, float y, float width, float height,
	                  unsigned int fill, const char *key, const char *label,
	                  int active) {
	unsigned int foreground = ui_contrast_bw(fill);
	ui_panel(x, y, width, height, fill,
	         active ? VT_THEME_BLUE_LIGHT : VT_THEME_BORDER, active);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	float key_width = 0.0f;
	if (small && key && key[0]) {
		key_width = (float)ui_font_text_width(small, UI_FONT_SMALL, key) + 14.0f;
		if (key_width < 34.0f) key_width = 34.0f;
	}
	if (key_width > 0.0f) {
		vita2d_draw_rectangle(x + 10, y + 9, key_width, height - 18,
		                      foreground == RGBA8(255, 255, 255, 255)
		                          ? RGBA8(255, 255, 255, 28)
		                          : RGBA8(0, 0, 0, 28));
		if (small) {
			int kw = ui_font_text_width(small, UI_FONT_SMALL, key);
			ui_font_draw_text(small, (int)(x + 10 + (key_width - kw) * .5f),
			                  (int)(y + height * .5f + 7), foreground,
			                  UI_FONT_SMALL, key);
		}
	}
	if (small && label) {
		int label_width = ui_font_text_width(small, UI_FONT_SMALL, label);
		float left = x + key_width + 17.0f;
		float available = width - key_width - 24.0f;
		ui_font_draw_text(small, (int)(left + (available - label_width) * .5f),
		                  (int)(y + height * .5f + 7), foreground,
		                  UI_FONT_SMALL, label);
	}
}
