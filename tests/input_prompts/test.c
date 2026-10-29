/* Actual production font and downloaded-sprite renderer, with FreeType metrics
 * and substituted Vita/GXM boundaries. No firmware rendering is claimed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <png.h>
#include "ui/font.h"
#include "i18n/strings.h"
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"Failed: %s, line %d\n",#x,__LINE__); exit(1); } } while(0)
static unsigned checks, sprite_calls, native_calls, loads, frees, alpha;
static FT_Library ft;
static FT_Face face;
static int native, end_x, start_x, baseline, slot_end;
static const char *asset_root;
struct HostTexture { unsigned char rgba[64*64*4]; };
static unsigned cp(const char **s) {
    const unsigned char *p=(const unsigned char*)*s;unsigned c=*p++;
    if(c>=0xc0&&c<0xe0)c=((c&31)<<6)|(*p++&63);
    else if(c>=0xe0){c=((c&15)<<6)|(*p++&63);c=(c<<6)|(*p++&63);}
    *s=(const char*)p;return c;
}
int vita2d_font_text_width(vita2d_font*f,unsigned size,const char*s) {
    (void)f;FT_Set_Pixel_Sizes(face,0,size);int w=0;
    while(*s){FT_Load_Char(face,cp(&s),FT_LOAD_DEFAULT);w+=face->glyph->advance.x>>6;}return w;
}
int vita2d_font_text_height(vita2d_font*f,unsigned size,const char*s){(void)f;(void)s;return size+4;}
int vita2d_font_draw_text(vita2d_font*f,int x,int y,unsigned c,unsigned size,const char*s) {
    (void)y;(void)c;CHECK(!ui_input_prompt_contains(s));CHECK(x>=slot_end);
    int w=vita2d_font_text_width(f,size,s);end_x=x+w;slot_end=end_x;return w;
}
int vita2d_pgf_text_width(vita2d_pgf*f,float z,const char*s){return vita2d_font_text_width(f,(unsigned)(20*z),s);}
int vita2d_pgf_text_height(vita2d_pgf*f,float z,const char*s){(void)f;(void)s;return 20*z;}
int vita2d_pgf_draw_text(vita2d_pgf*f,int x,int y,unsigned c,float z,const char*s){return vita2d_font_draw_text(f,x,y,c,20*z,s);}
vita2d_pgf *vita2d_load_system_pgf(int n,const vita2d_system_pgf_config*c){(void)n;(void)c;return NULL;}
void vita2d_free_pgf(vita2d_pgf*f){(void)f;}
int sceSysmoduleLoadModule(int id){(void)id;return -1;}
int sceRegMgrSystemParamGetInt(int k,int*v){(void)k;*v=0;return 0;}
void log_printf(const char*f,...){(void)f;}
int ui_native_vita_active(void){return native;}
int ui_native_vita_text_span(float x,float y,unsigned z){(void)x;(void)y;(void)z;return 300;}
int ui_native_vita_text(float x,float y,float w,unsigned s,unsigned c,const char*t){(void)x;(void)y;(void)w;(void)s;(void)c;CHECK(!ui_input_prompt_contains(t));native_calls++;return 1;}
void ui_native_vita_cover(float x,float y,float w,float h,unsigned c){(void)y;(void)h;(void)c;CHECK(x==start_x);CHECK(w>=0);}
vita2d_texture *vita2d_load_PNG_file(const char *path) {
    char local[2048];snprintf(local,sizeof(local),"%s/%s",asset_root,strrchr(path,'/')+1);
    png_image im={.version=PNG_IMAGE_VERSION};CHECK(png_image_begin_read_from_file(&im,local));
    im.format=PNG_FORMAT_RGBA;CHECK(im.width==64&&im.height==64);
    struct HostTexture*t=calloc(1,sizeof(*t));CHECK(t);CHECK(png_image_finish_read(&im,NULL,t->rgba,0,NULL));
    png_image_free(&im);loads++;return t;
}
void vita2d_free_texture(vita2d_texture*t){free(t);frees++;}
void vita2d_draw_fill_circle(float x,float y,float r,unsigned c){(void)x;(void)y;(void)r;(void)c;}
void vita2d_draw_rectangle(float x,float y,float w,float h,unsigned c){(void)x;(void)y;(void)w;(void)h;(void)c;}
void vita2d_draw_texture_tint_part_scale(const vita2d_texture*t,float x,float y,float tx,float ty,float tw,float th,float sx,float sy,unsigned tint) {
    CHECK(t&&sx>0&&sy>0);CHECK(x>=slot_end);CHECK(y>=baseline-40&&y+th*sy<=baseline+4);
    CHECK(tx>=0&&ty>=0&&tx+tw<=64&&ty+th<=64);
    end_x=(int)ceilf(x+tw*sx);slot_end=end_x;alpha=tint>>24;sprite_calls++;
}
static const char *const copy[][2]={
#define VT_STR(id,it,en) {it,en},
#include "i18n/strings.def"
#undef VT_STR
};
static void reset(void){start_x=10;end_x=slot_end=start_x;baseline=60;}
int main(int argc,char**argv){
    CHECK(argc==3);asset_root=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
    CHECK(ui_input_prompts_load()==0&&loads==6);CHECK(ui_input_prompts_load()==0&&loads==6);
    const char*sample=UI_PAD_CROSS " Open  " UI_PAD_CIRCLE " Back  " UI_PAD_SQUARE " Star  " UI_PAD_TRIANGLE " Search  " UI_PAD_L1 "/" UI_PAD_R1;
    for(unsigned z=12;z<=28;z+=4)for(native=0;native<2;native++) {
        reset();unsigned before=sprite_calls, before_native=native_calls;int width=ui_font_text_width(face,z,sample);
        CHECK(ui_font_draw_text(face,10,baseline,0x80ffffff,z,sample)==width);
        CHECK(sprite_calls==before+6&&end_x<=10+width&&end_x>=10+width-(int)z/2&&alpha==128);
        CHECK(native_calls==before_native);
        for(int limit=0;limit<400;limit++){
            char fitted[512];int w=ui_font_fit_text(face,z,sample,fitted,sizeof(fitted),limit);
            CHECK(w<=limit);CHECK(w==ui_font_text_width(face,z,fitted));
            reset();CHECK(ui_font_draw_text(face,10,baseline,0xffffffff,z,fitted)==w);
            CHECK(end_x<=10+w);
        }
    }
    native=0;
    const struct {int id, limit;} hints[]={
        {VT_STR_PLAYER_PANEL_HINT,374},{VT_STR_MUSIC_PLAYER_PANEL_HINT,374},
        {VT_STR_PLAYER_SHORTCUTS_HINT,400},{VT_STR_PLAYER_REMOTE_SHORTCUTS_HINT,400},
        {VT_STR_PLAYER_INPUT_LOCK_HINT,400},{VT_STR_PLAYER_SWAPPED_SHORTCUTS_HINT,400},
        {VT_STR_IPTV_PLAYER_HINT,400},{VT_STR_IPTV_VOD_HINT,400},
        {VT_STR_IMAGE_VIEWER_TOUCH_HINT,250},{VT_STR_IMAGE_VIEWER_HARDWARE_HINT,640}
    };
    for(size_t i=0;i<sizeof(hints)/sizeof(hints[0]);i++)for(int lang=0;lang<2;lang++){
        int width=ui_font_text_width(face,16,copy[hints[i].id][lang]);
        if(width>hints[i].limit)fprintf(stderr,"Hint exceeds %d: %dpx, language %d: %s\n",hints[i].limit,width,lang,copy[hints[i].id][lang]);
        CHECK(width<=hints[i].limit);
    }
    /* Every translated static line, including long guide paragraphs, is fitted
     * through the production UTF-8 path, not a replacement test algorithm. */
    for(size_t i=0;i<sizeof(copy)/sizeof(copy[0]);i++)for(int lang=0;lang<2;lang++){
        char fitted[512];int w=ui_font_fit_text(face,16,copy[i][lang],fitted,sizeof(fitted),374);
        CHECK(w<=374);reset();CHECK(ui_font_draw_text(face,10,baseline,0xffffffff,16,fitted)==w);
        CHECK(end_x<=10+w);
    }
    const char*literal="X O L1 R1 X-Men movie.mkv";
    unsigned before=sprite_calls;reset();ui_font_draw_text(face,10,baseline,0xffffffff,16,literal);CHECK(sprite_calls==before);
    reset();baseline=4;ui_font_draw_text(face,10,baseline,0xffffffff,16,sample);
    native=1;reset();before=native_calls;
    int clock_width=ui_font_draw_text_exact(face,10,baseline,0xffffffff,16,"11:59 PM");
    CHECK(end_x==10+clock_width&&native_calls==before);
    ui_input_prompts_free();ui_input_prompts_free();CHECK(frees==6);
    FT_Done_Face(face);FT_Done_FreeType(ft);
    printf("Input prompts: %u checks; production font/sprites, exact advances, fitting, alpha, native dispatch, all IT/EN copy and literal filenames passed.\n",checks);
}

vita2d_font *ui_runtime_exact_font(vita2d_font *f,unsigned size){(void)size;return f;}
