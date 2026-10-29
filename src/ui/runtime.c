#include "ui/runtime.h"

#include "common/text_log.h"
#include "ui/theme.h"
#include "ui/loading_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
/* libvita2d keys its atlas by glyph, not pixel size. Keep uncommon sizes
 * isolated too, including the shrinking header and menu captions. */
static struct { vita2d_font *font; unsigned size; int family; } extra_fonts[48];
static int font_family(vita2d_font *f, unsigned *pixels) {
 if(!f)return -1;
 if(f==g_font_small){*pixels=16;return 0;}
 if(f==g_font_body){*pixels=20;return 0;}
 if(f==g_font_display){*pixels=28;return 1;}
 if(f==g_theme_hero){*pixels=48;return 1;}
 for(int i=0;i<3;i++)if(f==g_theme_mono[i]){*pixels=i==0?16:i==1?20:28;return 2;}
 vita2d_font *sub[]={g_subtitle_medium_intermediate,g_subtitle_medium_display,g_subtitle_medium_xlarge,
  g_subtitle_semibold_small,g_subtitle_semibold_body,g_subtitle_semibold_intermediate,g_subtitle_semibold_display,g_subtitle_semibold_xlarge};
 const unsigned sizes[]={26,32,40,16,20,26,32,40};
 for(unsigned i=0;i<sizeof(sub)/sizeof(*sub);i++)if(f==sub[i]){*pixels=sizes[i];return i<3?0:1;}
 for(unsigned i=0;i<48;i++)if(f==extra_fonts[i].font){*pixels=extra_fonts[i].size;return extra_fonts[i].family;}
 return -1;
}

/* Keep one immutable copy of each packaged face. FreeType's memory faces
 * borrow these bytes, so they live until ALL independent size atlases close. */
static struct { const char *path; void *bytes; unsigned size; int attempted; } font_data[]={
 {"app0:fonts/Inter-Medium.ttf",NULL,0,0},
 {"app0:fonts/Inter-SemiBold.ttf",NULL,0,0},
 {"app0:fonts/IBMPlexMono-Regular.ttf",NULL,0,0}
};
static vita2d_font *cached_font(const char *path){
 for(unsigned i=0;i<sizeof(font_data)/sizeof(*font_data);i++)if(!strcmp(path,font_data[i].path)){
  if(!font_data[i].attempted){
   font_data[i].attempted=1;
   FILE *f=fopen(path,"rb");
   if(f){
    if(!fseek(f,0,SEEK_END)){
     long n=ftell(f);
     if(n>0&&n<=4*1024*1024&&!fseek(f,0,SEEK_SET)){
      void *bytes=malloc((size_t)n);
      if(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n){font_data[i].bytes=bytes;font_data[i].size=(unsigned)n;}
      else free(bytes);
     }
    }
    fclose(f);
   }
  }
  if(font_data[i].bytes)return vita2d_load_font_mem(font_data[i].bytes,font_data[i].size);
  break;
 }
 return vita2d_load_font_file(path);
}
vita2d_font *ui_runtime_exact_font(vita2d_font *font,unsigned size){
 unsigned pixels=0;int family=font_family(font,&pixels);
 if(family<0||pixels==size)return font;
 for(unsigned i=0;i<48;i++)if(extra_fonts[i].font&&extra_fonts[i].family==family&&extra_fonts[i].size==size)return extra_fonts[i].font;
 for(unsigned i=0;i<48;i++)if(!extra_fonts[i].font){
  vita2d_font *exact=cached_font(font_data[family].path);
  if(exact){extra_fonts[i].font=exact;extra_fonts[i].size=size;extra_fonts[i].family=family;return exact;}
  break;
 }
 /* Prefer the system font to scaling a glyph from a different atlas size. */
 return NULL;
}
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
		g_font_small = cached_font("app0:fonts/Inter-Medium.ttf");
	if (!g_logo) g_logo = vita2d_load_PNG_file("app0:sce_sys/icon0.png");
	ui_input_prompts_load();
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
		int fallback_ret;
        do {ui_loading_present(NULL);fallback_ret=ui_font_fallback_init_step();} while(fallback_ret==0);
		log_printf("system font: Latin/J/C/K fallback -> 0x%08X mask=0x%X",
		           (unsigned)fallback_ret,
		           (unsigned)ui_font_fallback_language_mask());
	}
    /* First UI needs only three exact-size faces. Subtitle-only atlases are
     * allocated on demand, never sixteen times before the first Home frame. */
    ui_loading_present(NULL);
    if(!g_font_body)g_font_body=cached_font("app0:fonts/Inter-Medium.ttf");
    ui_loading_present(NULL);
    if(!g_font_display)g_font_display=cached_font("app0:fonts/Inter-SemiBold.ttf");
    ui_loading_present(NULL);
    ui_runtime_prepare_theme();
}

void ui_runtime_prepare_theme(void){
 if(!g_runtime_ready)return;
 if(ui_theme_id()==VT_UI_THEME_DECK_90){
  for(int i=0;i<3;i++)if(!g_theme_mono[i])g_theme_mono[i]=cached_font("app0:fonts/IBMPlexMono-Regular.ttf");
 }
 if((ui_theme_id()==VT_UI_THEME_SIGNAL||ui_theme_id()==VT_UI_THEME_APERTURE||ui_theme_id()==VT_UI_THEME_GHOST_MESH)&&!g_theme_hero)
  g_theme_hero=cached_font("app0:fonts/Inter-SemiBold.ttf");
 vita2d_set_clear_color(VT_THEME_BG);
}
void ui_runtime_term(void) {
	if (g_runtime_ready) {
		vita2d_wait_rendering_done();
		ui_input_prompts_free();
        for(unsigned i=0;i<48;i++){if(extra_fonts[i].font)vita2d_free_font(extra_fonts[i].font);memset(&extra_fonts[i],0,sizeof(extra_fonts[i]));}
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
        for(unsigned i=0;i<sizeof(font_data)/sizeof(*font_data);i++){
            free(font_data[i].bytes);font_data[i].bytes=NULL;font_data[i].size=0;font_data[i].attempted=0;
        }
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
 return ui_runtime_exact_font(base_ui_font(size),size);
}

vita2d_font *ui_runtime_subtitle_font(int variant, unsigned int size) {
 int preference=variant>=3&&variant<=6?variant-2:UI_FONT_SYSTEM_AUTO;
 ui_font_set_system_preference(preference);
 if(variant>=2)return ui_font_fallback_ready()?NULL:base_ui_font(size);
 vita2d_font **slot=NULL;
 if(variant==1){
  slot=size==UI_FONT_SMALL?&g_subtitle_semibold_small:size==UI_FONT_BODY?&g_subtitle_semibold_body:
       size==UI_FONT_SUBTITLE_LARGE?&g_subtitle_semibold_display:
       size==UI_FONT_SUBTITLE_MEDIUM_LARGE?&g_subtitle_semibold_intermediate:
       size==UI_FONT_SUBTITLE_EXTRA_LARGE?&g_subtitle_semibold_xlarge:NULL;
 }else{
  slot=size==UI_FONT_SUBTITLE_LARGE?&g_subtitle_medium_display:
       size==UI_FONT_SUBTITLE_MEDIUM_LARGE?&g_subtitle_medium_intermediate:
       size==UI_FONT_SUBTITLE_EXTRA_LARGE?&g_subtitle_medium_xlarge:NULL;
 }
 if(slot){if(!*slot)*slot=cached_font(variant==1?"app0:fonts/Inter-SemiBold.ttf":"app0:fonts/Inter-Medium.ttf");if(*slot)return *slot;}
 return base_ui_font(size);
}

vita2d_font *ui_runtime_subtitle_scaled_font(int variant, unsigned int size) {
	vita2d_font *fallback = ui_runtime_subtitle_font(variant, size);
	if (variant >= 2) return fallback;
    int index=variant==1;
    if(!g_subtitle_scaled[index])g_subtitle_scaled[index]=cached_font(index?"app0:fonts/Inter-SemiBold.ttf":"app0:fonts/Inter-Medium.ttf");
	return g_subtitle_scaled[variant == 1] ? g_subtitle_scaled[variant == 1]
	                                      : fallback;
}

vita2d_texture *ui_runtime_logo(void) {
	return g_logo;
}
