#include "ui/runtime.h"

#include "common/text_log.h"
#include "ui/theme.h"

static int g_runtime_ready = 0;
static vita2d_font *g_font_small = NULL;
static vita2d_font *g_font_body = NULL;
static vita2d_font *g_font_display = NULL;
static vita2d_font *g_subtitle_medium_display = NULL;
static vita2d_font *g_subtitle_medium_xlarge = NULL;
static vita2d_font *g_subtitle_semibold_small = NULL;
static vita2d_font *g_subtitle_semibold_body = NULL;
static vita2d_font *g_subtitle_semibold_display = NULL;
static vita2d_font *g_subtitle_semibold_xlarge = NULL;
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
	/* libvita2d's atlas key does not include pixel size. Every subtitle tier
	 * needs its own face instance, otherwise the first cached bitmap is rescaled
	 * by the GPU and the larger captions become soft. */
	if (!g_font_small) g_font_small = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_font_body) g_font_body = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_font_display) g_font_display = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
	if (!g_subtitle_medium_display)
		g_subtitle_medium_display = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_subtitle_medium_xlarge)
		g_subtitle_medium_xlarge = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_subtitle_semibold_small)
		g_subtitle_semibold_small = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
	if (!g_subtitle_semibold_body)
		g_subtitle_semibold_body = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
	if (!g_subtitle_semibold_display)
		g_subtitle_semibold_display = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
	if (!g_subtitle_semibold_xlarge)
		g_subtitle_semibold_xlarge = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
	if (!g_font_small || !g_font_body || !g_font_display)
		log_printf("ui asset warning: one or more Inter font instances unavailable");
	if (!g_logo) g_logo = vita2d_load_PNG_file("app0:sce_sys/icon0.png");
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
		if (g_subtitle_medium_display) vita2d_free_font(g_subtitle_medium_display);
		if (g_subtitle_medium_xlarge) vita2d_free_font(g_subtitle_medium_xlarge);
		if (g_subtitle_semibold_small) vita2d_free_font(g_subtitle_semibold_small);
		if (g_subtitle_semibold_body) vita2d_free_font(g_subtitle_semibold_body);
		if (g_subtitle_semibold_display) vita2d_free_font(g_subtitle_semibold_display);
		if (g_subtitle_semibold_xlarge) vita2d_free_font(g_subtitle_semibold_xlarge);
		g_font_small = NULL;
		g_font_body = NULL;
		g_font_display = NULL;
		g_subtitle_medium_display = NULL;
		g_subtitle_medium_xlarge = NULL;
		g_subtitle_semibold_small = NULL;
		g_subtitle_semibold_body = NULL;
		g_subtitle_semibold_display = NULL;
		g_subtitle_semibold_xlarge = NULL;
		ui_font_fallback_term();
		vita2d_fini();
		g_runtime_ready = 0;
	}
}

int ui_runtime_is_ready(void) {
	return g_runtime_ready;
}

vita2d_font *ui_runtime_font(unsigned int size) {
	ui_font_set_system_preference(UI_FONT_SYSTEM_AUTO);
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

vita2d_font *ui_runtime_subtitle_font(int variant, unsigned int size) {
	int system_preference = UI_FONT_SYSTEM_AUTO;
	if (variant == 3) system_preference = UI_FONT_SYSTEM_JAPANESE;
	else if (variant == 4) system_preference = UI_FONT_SYSTEM_CHINESE;
	else if (variant == 5) system_preference = UI_FONT_SYSTEM_KOREAN;
	else if (variant == 6) system_preference = UI_FONT_SYSTEM_LATIN;
	ui_font_set_system_preference(system_preference);
	/* Variants 2+ are native Vita system faces. A NULL packaged face is the
	 * established signal that makes the mixed renderer select its Latin/J/C/K
	 * PGFs directly. Fall back to Inter only if PGF initialization failed. */
	if (variant >= 2)
		return ui_font_fallback_ready() ? NULL : ui_runtime_font(size);
	if (variant == 1) {
		vita2d_font *semibold = size == UI_FONT_SMALL ? g_subtitle_semibold_small
		                       : size == UI_FONT_BODY ? g_subtitle_semibold_body
		                       : size == UI_FONT_SUBTITLE_LARGE
		                           ? g_subtitle_semibold_display
		                       : size == UI_FONT_SUBTITLE_EXTRA_LARGE
		                           ? g_subtitle_semibold_xlarge : NULL;
		if (semibold) return semibold;
	}
	if (size == UI_FONT_SUBTITLE_LARGE && g_subtitle_medium_display)
		return g_subtitle_medium_display;
	if (size == UI_FONT_SUBTITLE_EXTRA_LARGE && g_subtitle_medium_xlarge)
		return g_subtitle_medium_xlarge;
	return ui_runtime_font(size);
}

vita2d_texture *ui_runtime_logo(void) {
	return g_logo;
}
