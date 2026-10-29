/* Actual runtime resource ownership, using small deterministic font/GXM stubs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ui/runtime.h"
#include "ui/theme.h"
#include "common/text_log.h"
#define SCE_GXM_MULTISAMPLE_2X 2
static unsigned frames,files,faces,frees,fallback_steps,prompts;
static int theme=7;
static unsigned palette[UI_COLOR_COUNT];
const unsigned *ui_theme_colors=palette;
int ui_theme_id(void){return theme;}
void ui_theme_apply(int n){theme=n;}
int vt_preferences_interface_theme(void){return theme;}
int vita2d_init_advanced_with_msaa(unsigned n,int m){assert(n&&m==2);return 1;}
void *vita2d_get_context(void){return palette;}
void *vita2d_get_current_fb(void){return palette;}
void vita2d_set_clear_color(unsigned n){(void)n;}
void vita2d_wait_rendering_done(void){}
void vita2d_fini(void){}
static vita2d_font *face(const void *bytes){void **p=malloc(sizeof(*p));assert(p);*p=(void*)bytes;faces++;return (void*)p;}
vita2d_font *vita2d_load_font_mem(const void *bytes,unsigned n){assert(bytes&&n>0);return face(bytes);}
vita2d_font *vita2d_load_font_file(const char *path){(void)path;assert(!"Unexpected font cache miss");return NULL;}
void vita2d_free_font(vita2d_font *font){assert(font);assert(*(void**)font);frees++;free(font);}
vita2d_texture *vita2d_load_PNG_file(const char *p){assert(p);return (void*)palette;}
void vita2d_free_texture(vita2d_texture *p){assert(p==(void*)palette);}
int ui_input_prompts_load(void){prompts++;return 0;}
void ui_input_prompts_free(void){}
void ui_font_set_system_preference(int n){(void)n;}
int ui_font_fallback_ready(void){return fallback_steps>=5;}
int ui_font_fallback_init_step(void){assert(frames>=fallback_steps);return ++fallback_steps==5?1:0;}
int ui_font_fallback_language_mask(void){return 15;}
void ui_font_fallback_term(void){fallback_steps=0;}
void ui_loading_present(const char *s){(void)s;frames++;}
void log_printf(const char *s,...){(void)s;}
static FILE *host_fopen(const char *path,const char *mode){
 assert(!strcmp(mode,"rb"));assert(!strncmp(path,"app0:fonts/",11));files++;
 char local[160];snprintf(local,sizeof(local),"assets/fonts/%s",path+11);return fopen(local,mode);
}
#define fopen host_fopen
#include "../../src/ui/runtime.c"
#undef fopen
int main(void){
 assert(!ui_runtime_init());ui_runtime_load_boot_assets();
 assert(files==1&&faces==1&&prompts==1);
 ui_runtime_load_assets();assert(files==2&&faces==3&&frames>=8&&fallback_steps==5);
 unsigned before_faces=faces,before_files=files;
 ui_runtime_load_assets();assert(faces==before_faces&&files==before_files);
 const unsigned sizes[]={16,20,26,32,40};
 for(int weight=0;weight<2;weight++)for(unsigned i=0;i<5;i++){
  vita2d_font *font=ui_runtime_subtitle_font(weight,sizes[i]);assert(font);
  assert(ui_runtime_subtitle_font(weight,sizes[i])==font);
 }
 assert(files==2);assert(ui_runtime_subtitle_font(3,32)==NULL);
 vita2d_font *body=ui_runtime_font(20),*display=ui_runtime_font(28),*exact[13];
 for(unsigned n=16;n<=28;n++){
  exact[n-16]=ui_runtime_exact_font(body,n);assert(exact[n-16]);
  assert(exact[n-16]==ui_runtime_exact_font(body,n));
  for(unsigned j=16;j<n;j++)assert(exact[j-16]!=exact[n-16]);
  assert(ui_runtime_exact_font(display,n)!=exact[n-16]);
 }
 assert(ui_runtime_exact_font(NULL,20)==NULL&&files==2);
 assert(ui_runtime_subtitle_scaled_font(0,20));assert(ui_runtime_subtitle_scaled_font(1,20));
 theme=VT_UI_THEME_DECK_90;ui_runtime_prepare_theme();assert(files==3);
 ui_runtime_term();assert(frees==faces);
 for(unsigned i=0;i<3;i++)assert(!font_data[i].bytes&&!font_data[i].attempted);
 puts("Startup runtime: cached face bytes, three boot atlases, staged CJK frames, lazy exact-size subtitles, per-theme fonts and borrow lifetimes passed.");
}
