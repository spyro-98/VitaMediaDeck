#include "ui/runtime.h"

#include "common/text_log.h"
#include "ui/theme.h"

static int g_runtime_ready = 0;
static vita2d_font *g_font_small = NULL;
static vita2d_font *g_font_body = NULL;
static vita2d_font *g_font_display = NULL;
static vita2d_texture *g_logo = NULL;

int ui_runtime_init(void) {
	if (g_runtime_ready) return 0;

	/* Do not initialize AppUtil/CommonDialog here. This combination was
	 * already tried on hardware on 2026-08-04 and prevented the app from
	 * starting. The keyboard uses sceIme directly and shares this framebuffer. */
	int ret = vita2d_init();
	/* libvita2d returns 1 even when many internal GXM calls have failed:
	 * the return value alone isn't a useful postcondition. */
	if (ret <= 0 || vita2d_get_context() == NULL ||
	    vita2d_get_current_fb() == NULL) {
		return -1;
	}

	vita2d_set_clear_color(VT_THEME_BG);
	g_runtime_ready = 1;
	return 0;
}

void ui_runtime_load_assets(void) {
	if (!g_runtime_ready) return;
	/* Keep the system PGFs available for complete Japanese/Chinese/Korean
	 * coverage and for codepoints absent from the packaged face. */
	if (!ui_font_fallback_ready()) {
		int fallback_ret = ui_font_fallback_init();
		log_printf("system font: Latin/J/C/K fallback -> 0x%08X mask=0x%X",
		           (unsigned)fallback_ret,
		           (unsigned)ui_font_fallback_language_mask());
	}
	/* libvita2d's atlas key does not include pixel size. These must stay three
	 * independent instances, otherwise the first cached bitmap is rescaled by
	 * the GPU and later sizes become soft. */
	if (!g_font_small) g_font_small = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_font_body) g_font_body = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_font_display) g_font_display = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
	if (!g_font_small || !g_font_body || !g_font_display)
		log_printf("ui asset warning: one or more Inter font instances unavailable");
	if (!g_logo) g_logo = vita2d_load_PNG_file("app0:sce_sys/vitawavelogoalpha.png");
	if (!g_logo) log_printf("ui asset warning: brand logo unavailable, using vector fallback");
}

void ui_runtime_term(void) {
	if (g_runtime_ready) {
		vita2d_wait_rendering_done();
		if (g_logo) {
			vita2d_free_texture(g_logo);
			g_logo = NULL;
		}
		if (g_font_small) vita2d_free_font(g_font_small);
		if (g_font_body) vita2d_free_font(g_font_body);
		if (g_font_display) vita2d_free_font(g_font_display);
		g_font_small = NULL;
		g_font_body = NULL;
		g_font_display = NULL;
		ui_font_fallback_term();
		vita2d_fini();
		g_runtime_ready = 0;
	}
}

int ui_runtime_is_ready(void) {
	return g_runtime_ready;
}

vita2d_font *ui_runtime_font(unsigned int size) {
	vita2d_font *preferred = size == UI_FONT_SMALL ? g_font_small
	                       : size == UI_FONT_BODY ? g_font_body
	                       : size == UI_FONT_DISPLAY ? g_font_display : NULL;
	if (preferred) return preferred;
	/* A single allocation failure must not blank an entire text tier.  Reusing
	 * another exact-face instance is a degraded (potentially softer) fallback,
	 * but remains readable and lets the mixed renderer reach the system PGFs. */
	if (g_font_body) return g_font_body;
	if (g_font_small) return g_font_small;
	return g_font_display;
}

vita2d_texture *ui_runtime_logo(void) {
	return g_logo;
}
