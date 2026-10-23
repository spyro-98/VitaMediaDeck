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
static vita2d_font *g_subtitle_medium_intermediate = NULL;
static vita2d_font *g_subtitle_semibold_intermediate = NULL;
static vita2d_font *g_subtitle_scaled[2];
static vita2d_texture *g_logo = NULL;
static vita2d_font *g_theme_mono[3], *g_theme_hero;

int ui_runtime_init(void) {
	if (g_runtime_ready) return 0;

	/* Do not initialize AppUtil/CommonDialog here. This combination was
	 * already tried on hardware on 2026-08-04 and prevented the app from
	 * starting. The keyboard uses sceIme directly and shares this framebuffer. */
	/* Native 960x544 output with GPU edge coverage; no oversized render target
     * or additional full-screen supersampling pass. */
	int ret = vita2d_init_advanced_with_msaa(1024u*1024u,SCE_GXM_MULTISAMPLE_2X);
	/* libvita2d returns 1 even when many internal GXM calls have failed:
	 * the return value alone isn't a useful postcondition. */
	if (ret <= 0 || vita2d_get_context() == NULL ||
	    vita2d_get_current_fb() == NULL) {
		return -1;
	}

	ui_theme_apply(vt_preferences_interface_theme());
	vita2d_set_clear_color(VT_THEME_BG);
	g_runtime_ready = 1;
	return 0;
}

void ui_runtime_load_boot_assets(void) {
	if (!g_runtime_ready) return;
	if (!g_font_small)
		g_font_small = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_logo) g_logo = vita2d_load_PNG_file("app0:sce_sys/icon0.png");
	if (!g_font_small)
		log_printf("ui boot asset warning: Inter font unavailable");
	if (!g_logo)
		log_printf("ui boot asset warning: brand logo unavailable, using vector fallback");
}

void ui_runtime_load_assets(void) {
	if (!g_runtime_ready) return;
	ui_runtime_load_boot_assets();
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
	ui_runtime_prepare_theme();
	if (!g_font_small || !g_font_body || !g_font_display)
		log_printf("ui asset warning: one or more Inter font instances unavailable");
	if (!g_subtitle_medium_intermediate)
		g_subtitle_medium_intermediate = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_subtitle_semibold_intermediate)
		g_subtitle_semibold_intermediate = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
	if (!g_subtitle_scaled[0])
		g_subtitle_scaled[0] = vita2d_load_font_file("app0:fonts/Inter-Medium.ttf");
	if (!g_subtitle_scaled[1])
		g_subtitle_scaled[1] = vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
}

void ui_runtime_prepare_theme(void){
 if(!g_runtime_ready)return;
 if(ui_theme_id()==VT_UI_THEME_DECK_90){
  for(int i=0;i<3;i++)if(!g_theme_mono[i])g_theme_mono[i]=vita2d_load_font_file("app0:fonts/IBMPlexMono-Regular.ttf");
 }
 if((ui_theme_id()==VT_UI_THEME_SIGNAL||ui_theme_id()==VT_UI_THEME_APERTURE||ui_theme_id()==VT_UI_THEME_GHOST_MESH)&&!g_theme_hero)
  g_theme_hero=vita2d_load_font_file("app0:fonts/Inter-SemiBold.ttf");
 vita2d_set_clear_color(VT_THEME_BG);
}
void ui_runtime_term(void) {
	if (g_runtime_ready) {
		vita2d_wait_rendering_done();
		for(int i=0;i<3;i++)if(g_theme_mono[i]){vita2d_free_font(g_theme_mono[i]);g_theme_mono[i]=NULL;}
		if(g_theme_hero){vita2d_free_font(g_theme_hero);g_theme_hero=NULL;}
		if (g_logo) {
			vita2d_free_texture(g_logo);
			g_logo = NULL;
		}
		if (g_font_small) vita2d_free_font(g_font_small);
		if (g_subtitle_medium_intermediate) vita2d_free_font(g_subtitle_medium_intermediate);
		if (g_subtitle_semibold_intermediate) vita2d_free_font(g_subtitle_semibold_intermediate);
		g_subtitle_medium_intermediate = NULL;
		g_subtitle_semibold_intermediate = NULL;
		for (int i = 0; i < 2; i++) {
			if (g_subtitle_scaled[i]) vita2d_free_font(g_subtitle_scaled[i]);
			g_subtitle_scaled[i] = NULL;
		}
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

static vita2d_font *base_ui_font(unsigned int size) {
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

vita2d_font *ui_runtime_font(unsigned int size) {
 ui_font_set_system_preference(UI_FONT_SYSTEM_AUTO);
 if(size==UI_FONT_HERO&&g_theme_hero)return g_theme_hero;
 if(ui_theme_id()==VT_UI_THEME_DECK_90){
  int i=size==UI_FONT_SMALL?0:size==UI_FONT_BODY?1:2;
  if(g_theme_mono[i])return g_theme_mono[i];
 }
 return base_ui_font(size);
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
		return ui_font_fallback_ready() ? NULL : base_ui_font(size);
	if (variant == 1) {
		if (size == UI_FONT_SUBTITLE_MEDIUM_LARGE && g_subtitle_semibold_intermediate)
			return g_subtitle_semibold_intermediate;
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
	if (size == UI_FONT_SUBTITLE_MEDIUM_LARGE && g_subtitle_medium_intermediate)
		return g_subtitle_medium_intermediate;
	if (size == UI_FONT_SUBTITLE_EXTRA_LARGE && g_subtitle_medium_xlarge)
		return g_subtitle_medium_xlarge;
	return base_ui_font(size);
}

vita2d_font *ui_runtime_subtitle_scaled_font(int variant, unsigned int size) {
	/* Small previews must never populate the fullscreen/body glyph atlases. */
	vita2d_font *fallback = ui_runtime_subtitle_font(variant, size);
	if (variant >= 2) return fallback;
	return g_subtitle_scaled[variant == 1] ? g_subtitle_scaled[variant == 1]
	                                      : fallback;
}

vita2d_texture *ui_runtime_logo(void) {
	return g_logo;
}
