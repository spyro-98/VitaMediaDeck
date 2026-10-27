/* Actual IPTV page, components and drawer; host FreeType rasterization.
 * System header/player are stubs. PNGs are layout checks, NOT Vita screenshots. */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <png.h>
#include "../../src/ui/theme.c"
#include "../../src/ui/theme_home.c"
#include "../../src/ui/theme_scene.c"
#include "../../src/ui/iptv_screen.c"
#include "../../src/ui/sections_sidebar.c"
#include "../../src/ui/components.c"
#include "../../src/ui/focus_glow.c"
#include "../../src/ui/breadcrumbs.c"
#include "../library/ui-stubs.h"

static unsigned checks, frame, language, live_slots, created, destroyed, plays, stops, saved;
static int download_slots=-1;
static unsigned draw_primitives, artwork_requests, gpu_draw_calls, gpu_vertex_count;
static vita2d_color_vertex gpu_first_vertices[8];
static int next_result, next_event, reconnect;
static VtIptvChannel stored_channel;
static int stored_result;
static const char *rename_input;
static int write_failure;
static unsigned removed;
static unsigned playback_notices;
static const char *playback_notice;
static int dash_selected;
static unsigned probe_calls;
static int probe_cancel_after, probe_connected=1;
int vita_https_is_connected(void){return probe_connected;}
int vt_iptv_http_probe(const VtIptvHttpOptions *opts,const char *url,volatile int *cancel) {
    (void)opts;(void)url;unsigned n=probe_calls++;
    if(probe_cancel_after && probe_calls==(unsigned)probe_cancel_after){*cancel=1;return VT_IPTV_PROBE_UNKNOWN;}
    return n%5;
}
static char last_stream_url[VT_IPTV_URL_MAX];
static FT_Library ft;
static FT_Face face;
static unsigned char pixels[544][960][3];
static unsigned char first_pixels[544][960][3];
static int online_case;
static int mini_top = 544, drawing_trail;
static char trail_text[2048];
static int tap_frame=-1, tap_x, tap_y;
static unsigned input_script[64], script_count, capture_frame;
static SceCtrlData input_axes[64];static unsigned axis_count;
static unsigned compact_home_labels;
static int allow_audio_fullscreen;
static int clipped, clip_left, clip_top, clip_right=960, clip_bottom=544;
static const char *output;
#define CHECK(x) do { ++checks; assert(x); } while (0)
void ui_message_show(const char *title,const char *detail,int ms) {
    CHECK(title && detail && ms>0);playback_notices++;playback_notice=detail;
}
static const char *english[] = {
#define VT_STR(id,it,en) [VT_STR_##id] = en,
#include "i18n/strings.def"
#undef VT_STR
};
static const char *italian[] = {
#define VT_STR(id,it,en) [VT_STR_##id] = it,
#include "i18n/strings.def"
#undef VT_STR
};
const char *vt_i18n_str(VtStringId id) { return language ? italian[id] : english[id]; }
static unsigned codepoint(const char **text) {
    const unsigned char *p=(const unsigned char *)*text;unsigned cp=*p++;
    if(cp>=0xc0&&cp<0xe0){cp=((cp&31)<<6)|(*p++&63);}
    else if(cp>=0xe0&&cp<0xf0){cp=((cp&15)<<6)|(*p++&63);cp=(cp<<6)|(*p++&63);}
    *text=(const char *)p;return cp;
}
vita2d_font *ui_runtime_font(unsigned size) { (void)size; return (void *)face; }
int ui_font_text_width(vita2d_font *font,unsigned size,const char *text) {
    (void)font;FT_Set_Pixel_Sizes(face,0,size);int width=0;
    while(*text){FT_Load_Char(face,codepoint(&text),FT_LOAD_DEFAULT);width+=(int)(face->glyph->advance.x>>6);}
    return width;
}
int ui_font_fit_text(vita2d_font *font,unsigned size,const char *text,char *out,size_t capacity,int max) {
    snprintf(out,capacity,"%s",text);size_t n=strlen(out);
    while(n&&ui_font_text_width(font,size,out)>max) {
        n--; while(n&&((unsigned char)out[n]&0xc0)==0x80) n--;
        out[n]=0;
    }
    return ui_font_text_width(font,size,out);
}
static void pixel(int x,int y,unsigned color,unsigned coverage) {
    if(x<0||y<0||x>=960||y>=544)return;
    if(clipped&&(x<clip_left||x>=clip_right||y<clip_top||y>=clip_bottom))return;
    unsigned alpha=(color>>24)*coverage/255;
    for(int c=0;c<3;c++)pixels[y][x][c]=(unsigned char)(
        (((color>>(c*8))&255)*alpha+pixels[y][x][c]*(255-alpha))/255);
}
int ui_font_draw_text(vita2d_font *font,int x,int y,unsigned color,unsigned size,const char *text) {
    if(y<54&&text&&(!strcmp(text,"Video locali")||!strcmp(text,"Libreria")||!strcmp(text,"Podcast")||!strcmp(text,"Radio")))compact_home_labels++;

    int width=ui_font_text_width(font,size,text);
    if (!clipped&&(x<0||y<(int)size||x+width>960||y>544))
        fprintf(stderr,"Text bounds: x=%d y=%d width=%d size=%u text=%s\n",x,y,width,size,text);
    CHECK(clipped||(x>=0&&y>=(int)size&&x+width<=960&&y<=544));
    CHECK(!strstr(text,"secret-token"));
    if(drawing_trail) {
        CHECK(x+width<=696);
        strncat(trail_text,text,sizeof(trail_text)-strlen(trail_text)-1);
    }
    while(*text) {
        FT_Load_Char(face,codepoint(&text),FT_LOAD_RENDER);
        FT_GlyphSlot g=face->glyph;
        for(unsigned row=0;row<g->bitmap.rows;row++)
            for(unsigned col=0;col<g->bitmap.width;col++)
                pixel(x+g->bitmap_left+(int)col,y-g->bitmap_top+(int)row,color,
                      g->bitmap.buffer[row*g->bitmap.pitch+col]);
        x+=(int)(g->advance.x>>6);
    }
    return width;
}
int ui_font_draw_textf(vita2d_font *font,int x,int y,unsigned color,unsigned size,const char *format,...) {
    char line[512];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    return ui_font_draw_text(font,x,y,color,size,line);
}
void vita2d_draw_rectangle(float x,float y,float w,float h,unsigned color) {
    draw_primitives++;
    for(int row=(int)y;row<(int)ceilf(y+h);row++)for(int col=(int)x;col<(int)ceilf(x+w);col++)
        pixel(col,row,color,255);
}
/* Software reference for the same GPU triangle list used by native vita2d. */
void vita2d_draw_array(SceGxmPrimitiveType mode,const vita2d_color_vertex *vertices,size_t count) {
    CHECK(mode==SCE_GXM_PRIMITIVE_TRIANGLES&&count%3==0&&count<=4096);draw_primitives++;
    if(gpu_draw_calls<8)gpu_first_vertices[gpu_draw_calls]=vertices[0];
    gpu_draw_calls++;gpu_vertex_count+=(unsigned)count;
    for(size_t i=0;i<count;i+=3) {
        const vita2d_color_vertex *a=&vertices[i],*b=&vertices[i+1],*c=&vertices[i+2];
        CHECK(isfinite(a->x)&&isfinite(a->y)&&isfinite(b->x)&&isfinite(b->y)&&isfinite(c->x)&&isfinite(c->y));
        int left=(int)floorf(fminf(a->x,fminf(b->x,c->x))),right=(int)ceilf(fmaxf(a->x,fmaxf(b->x,c->x)));
        int top=(int)floorf(fminf(a->y,fminf(b->y,c->y))),bottom=(int)ceilf(fmaxf(a->y,fmaxf(b->y,c->y)));
        float area=(b->x-a->x)*(c->y-a->y)-(b->y-a->y)*(c->x-a->x);
        if(fabsf(area)<.001f)continue;
        for(int y=top;y<bottom;y++)for(int x=left;x<right;x++) {
            float u=((b->x-x-.5f)*(c->y-y-.5f)-(b->y-y-.5f)*(c->x-x-.5f))/area;
            float v=((c->x-x-.5f)*(a->y-y-.5f)-(c->y-y-.5f)*(a->x-x-.5f))/area;
            if(u>=0&&v>=0&&u+v<=1)pixel(x,y,a->color,255);
        }
    }
}
void vita2d_start_drawing(void) {}
void vita2d_enable_clipping(void){clipped=1;}
void vita2d_disable_clipping(void){clipped=0;}
void vita2d_set_clip_rectangle(int a,int b,int c,int d){clip_left=a;clip_top=b;clip_right=c;clip_bottom=d;}
void vita2d_end_drawing(void) {}
void vita2d_wait_rendering_done(void) {}
void vita2d_clear_screen(void) { memset(pixels,0,sizeof(pixels)); }
void vita2d_swap_buffers(void) { if(frame==capture_frame)memcpy(first_pixels,pixels,sizeof(pixels));frame++;CHECK(frame<200); }
uint64_t sceKernelGetProcessTimeWide(void) { return frame*16667ULL+1; }
int sceKernelDelayThread(unsigned us) { (void)us;return 0; }
int sceCtrlSetSamplingMode(int mode) { (void)mode;return 0; }
int sceCtrlPeekBufferPositive(int port,SceCtrlData *pad,int count) {
    (void)port;(void)count;*pad=(SceCtrlData){.lx=128,.ly=128,.rx=128,.ry=128,
        .buttons=script_count?(frame<script_count?input_script[frame]:SCE_CTRL_CIRCLE):online_case?(frame==1?SCE_CTRL_LTRIGGER:frame==3?SCE_CTRL_DOWN:frame==5?SCE_CTRL_CROSS:0):frame?SCE_CTRL_CIRCLE:0};
    if(frame<axis_count){pad->lx=input_axes[frame].lx;pad->ly=input_axes[frame].ly;pad->rx=input_axes[frame].rx;pad->ry=input_axes[frame].ry;}
    return 1;
}
unsigned ui_touch_poll(UiTouchEvent *event) {
    memset(event,0,sizeof(*event));
    if((int)frame==tap_frame){event->x=event->down_x=tap_x;event->y=event->down_y=tap_y;tap_frame=-1;return UI_TOUCH_EVENT_TAP;}
    return 0;
}
void ui_touch_reset(void) {}
int ui_touch_hit_rect(int x,int y,int rx,int ry,int rw,int rh) {return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh;}
int ui_brand_sections_hit(unsigned flags,int x,int y) { (void)flags;(void)x;(void)y;return 0; }
void ui_brand_draw_header(const char *title) {
    ui_brand_draw_breadcrumbs(NULL,NULL,title);
}
#ifdef VMD_ONLINE_UI
void ui_brand_draw_home_header(void){ui_brand_draw_header(NULL);}
void ui_brand_draw_home_search(void){}
int ui_brand_home_search_hit(unsigned flags,int x,int y){return (flags&UI_TOUCH_EVENT_TAP)&&x>=78&&x<148&&y>=4&&y<50;}
#endif
/* Shared by every page: theme backgrounds also draw lines in Legacy. */
void vita2d_draw_line(float x,float y,float xx,float yy,unsigned color){
 draw_primitives++;
 int steps=(int)fmaxf(fabsf(xx-x),fabsf(yy-y))+1;
 for(int i=0;i<=steps;i++)pixel(x+(xx-x)*i/steps,y+(yy-y)*i/steps,color,255);
}
void ui_brand_draw_breadcrumbs(const char *section,const char *parent,const char *leaf) {
    vita2d_draw_rectangle(0,0,960,54,VT_THEME_SURFACE);
    drawing_trail=1;
    trail_text[0]=0;
    ui_breadcrumb_draw(68,696,section,parent,leaf,1);
    drawing_trail=0;
    /* Status space is deliberately reserved, not a simulated Vita readout. */
    vita2d_draw_rectangle(710,9,232,36,VT_THEME_BORDER_DIM);
}
void ui_mini_player_pump(void) {}
void ui_mini_player_draw(void) { if(mini_top<544)vita2d_draw_rectangle(0,mini_top,960,544-mini_top,VT_THEME_SURFACE_RAISED); }
int ui_mini_player_top(void) {return mini_top;}
int ui_mini_player_handle_buttons(unsigned *buttons) { (void)buttons;return 0; }
int ui_mini_player_handle_touch(unsigned flags,const UiTouchEvent *event) { (void)flags;(void)event;return 0; }
int ui_mini_player_input_locked(void) { return 0; }
static int host_reduced_motion=1;
int vt_preferences_reduce_motion(void) { return host_reduced_motion; }
int vt_background_playback_can_resume_fullscreen(void) { return live_slots; }
int vt_download_background_active(void) { return download_slots<0?(int)live_slots:download_slots; }
#ifndef VMD_ONLINE_UI
void ui_home_tab_set(int tab) { (void)tab; }
void ui_online_audio_set_home(int favorites) { (void)favorites; }
#endif
#ifndef VMD_CACHE_TRANSITION_TEST
int sceKernelWaitThreadEnd(SceUID id,void *a,void *b){(void)id;(void)a;(void)b;return 0;}
int sceKernelDeleteThread(SceUID id){(void)id;return 0;}
int sceKernelStartThread(SceUID id,SceSize n,void *ctx){(void)id;(void)n;(void)ctx;return -1;}
SceUID sceKernelCreateThread(const char *n,int(*f)(SceSize,void*),int p,size_t s,int a,int b,void *c){(void)n;(void)f;(void)p;(void)s;(void)a;(void)b;(void)c;return -1;}
#endif
int sceKernelExitThread(int result){return result;}
int ui_loading_run(const char *m,UiLoadingTaskFn task,void *ctx,volatile int *cancel,const volatile long *a,const volatile long *b){(void)m;(void)cancel;(void)a;(void)b;return task(ctx);}
int ui_qr_scan_https_url(char *out,size_t capacity){snprintf(out,capacity,"https://example.org/list.m3u");return 1;}
const char *ui_qr_scan_last_error(void){return "Camera unavailable";}
void vt_video_thumbnail_resume(void){}
void vt_video_thumbnail_pump(void){}
static struct {unsigned w,h;unsigned char rgba[64*128*4];} icon;
vita2d_texture *vt_artwork_thumbnail_get(const char *path) {
    if(!path || !path[0])return NULL;
    artwork_requests++;
    icon.w=icon.h=64;
    for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++) {
        unsigned char *p=icon.rgba+(y*64+x)*4;
        p[0]=y<8?240:25+x*2;p[1]=140;p[2]=y>=56?250:50+y*2;p[3]=255;
    }
    return (void*)&icon;
}
vita2d_texture *vt_image_thumbnail_get(const char *path,uint64_t size){
    (void)size;if(strncmp(path,"app0:flags/",11))return NULL;
    char local[256];snprintf(local,sizeof(local),"assets/%s",path+5);
    png_image im={.version=PNG_IMAGE_VERSION};if(!png_image_begin_read_from_file(&im,local))return NULL;
    im.format=PNG_FORMAT_RGBA;assert(PNG_IMAGE_SIZE(im)<=sizeof(icon.rgba));
    if(!png_image_finish_read(&im,NULL,icon.rgba,0,NULL)){png_image_free(&im);return NULL;}
    icon.w=im.width;icon.h=im.height;png_image_free(&im);return (void*)&icon;
}
vita2d_texture *vt_country_flag_get(const char *code) {
    if(strlen(code)!=2)return NULL;
    char path[64];snprintf(path,sizeof(path),"app0:flags/%c%c.png",code[0]+32,code[1]+32);
    return vt_image_thumbnail_get(path,0);
}
unsigned vita2d_texture_get_width(const vita2d_texture *t){(void)t;return icon.w;}
unsigned vita2d_texture_get_height(const vita2d_texture *t){(void)t;return icon.h;}

void vita2d_draw_array_textured(const vita2d_texture *tex,SceGxmPrimitiveType mode,const vita2d_texture_vertex *v,size_t count,unsigned tint) {
 (void)tex;CHECK(mode==SCE_GXM_PRIMITIVE_TRIANGLES&&count==6);draw_primitives++;
 for(size_t i=0;i<count;i+=3) {
  const vita2d_texture_vertex *a=v+i,*b=v+i+1,*c=v+i+2;
  float area=(b->x-a->x)*(c->y-a->y)-(b->y-a->y)*(c->x-a->x);if(fabsf(area)<.001f)continue;
  int left=fmaxf(0,floorf(fminf(a->x,fminf(b->x,c->x)))),right=fminf(960,ceilf(fmaxf(a->x,fmaxf(b->x,c->x))));
  int top=fmaxf(0,floorf(fminf(a->y,fminf(b->y,c->y)))),bottom=fminf(544,ceilf(fmaxf(a->y,fmaxf(b->y,c->y))));
  for(int y=top;y<bottom;y++)for(int x=left;x<right;x++) {
   float u=((b->x-x-.5f)*(c->y-y-.5f)-(b->y-y-.5f)*(c->x-x-.5f))/area;
   float w=((c->x-x-.5f)*(a->y-y-.5f)-(c->y-y-.5f)*(a->x-x-.5f))/area;
   if(u<0||w<0||u+w>1)continue;
   float ss=u*a->u+w*b->u+(1-u-w)*c->u,tt=u*a->v+w*b->v+(1-u-w)*c->v;
   int sx=fminf(icon.w-1,fmaxf(0,ss*icon.w)),sy=fminf(icon.h-1,fmaxf(0,tt*icon.h));
   unsigned char *p=icon.rgba+(sy*icon.w+sx)*4;
   pixel(x,y,RGBA8(p[0],p[1],p[2],p[3]),tint>>24);
  }
 }
}
void vita2d_draw_texture_scale(const vita2d_texture *t,float x,float y,float sx,float sy){
    (void)t;for(int row=0;row<(int)(icon.h*sy);row++)for(int col=0;col<(int)(icon.w*sx);col++){
        unsigned offset=((int)(row/sy)*icon.w+(int)(col/sx))*4;
        unsigned char *p=icon.rgba+offset;pixel(x+col,y+row,RGBA8(p[0],p[1],p[2],p[3]),255);
    }
}
void vt_http_audio_stream_factory(const char *url,VtDecoderStreamFactory *out){memset(out,0,sizeof(*out));out->opaque=(void*)url;}
int vt_hw_player_screen_run(const VtHwPlayerScreenSource *s,uint64_t*a,uint64_t*b,int*c,int*d,int*e){(void)s;(void)a;(void)b;(void)c;(void)d;(void)e;return next_result;}
int vt_online_fetch_cover(VtOnlineItem *item,volatile int *cancel){(void)item;(void)cancel;return -1;}
int vt_iptv_http_fetch(void*a,const char*b,size_t c,unsigned char**d,size_t*e,char*f,size_t g,volatile int*h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;return -1;}
int vt_iptv_saved_load(VtIptvCatalog *out,int stars){
    memset(out,0,sizeof(*out));
    if(stored_channel.url[0]&&(!stars||stored_channel.starred)){out->items[0]=stored_channel;out->count=1;}
    return stored_result;
}
int vt_iptv_saved_set(const VtIptvChannel *item){if(write_failure)return -1;stored_channel=*item;return 0;}
int vt_iptv_saved_remove(const char *url){if(write_failure)return -1;CHECK(!strcmp(url,stored_channel.url));memset(&stored_channel,0,sizeof(stored_channel));removed++;return 0;}
void vt_dash_source_configure(VtIptvSourceConfig *config){(void)config;dash_selected++;}
void vt_hls_source_configure(VtIptvSourceConfig *config){(void)config;}
int vt_iptv_http_fetch_range(void*a,const char*b,uint64_t c,size_t d,unsigned char**e,size_t*f,char*g,size_t h,volatile int*i){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;return -1;}
void vt_video_thumbnail_prepare_playback(void) {}
void vt_background_playback_set_fullscreen_resume(VtBackgroundFullscreenResume fn,void *ctx) { CHECK((!fn&&!ctx)||(allow_audio_fullscreen&&fn&&!ctx)); }
void vt_background_playback_stop(void) { stops++; }
void log_printf(const char *format,...) { CHECK(!strstr(format,"http")); }
int log_save(const char *path) { CHECK(!strcmp(path,VITAMEDIADECK_SESSION_LOG_PATH));saved++;return 0; }
int ui_text_input(const char *title,const char *initial,char *out,size_t capacity) {
    (void)title;(void)initial;
    if(!rename_input)return 0;
    snprintf(out,capacity,"%s",rename_input);return 1;
}
VtIptvSession *vt_iptv_session_create(const char *url,const VtIptvSourceConfig *config) {
    snprintf(last_stream_url,sizeof(last_stream_url),"%s",url);
    (void)config;CHECK(strstr(url,"https://"));created++;return (void *)(uintptr_t)created;
}
void vt_iptv_session_factory(VtIptvSession *s,VtDecoderStreamFactory *f) { memset(f,0,sizeof(*f));f->opaque=s; }
int vt_hw_player_screen_run_live(const VtHwPlayerScreenSource *s,VtIptvSession *session) {
    CHECK(s->favorite && s->favorite_ctx);
    CHECK(s->favorite(s->favorite_ctx,0)>=0);
    CHECK(s->stream.opaque == session && !s->allow_minimize && s->history_id &&
          s->authenticated_remote);
    CHECK(!s->start_position_ms&&!s->start_audio_track&&!s->start_subtitle_track);
    plays++;return reconnect&&plays==1?VT_HW_PLAYER_ACTION_GO_LIVE:next_result;
}
int vt_iptv_session_event(VtIptvSession *s) { (void)s;return next_event; }
void vt_iptv_session_status(VtIptvSession *s,VtIptvSourceStatus *status) { (void)s;memset(status,0,sizeof(*status)); }
void vt_iptv_session_destroy(VtIptvSession *s) { (void)s;destroyed++;CHECK(destroyed<=plays); }
static void png(const char *name) {
    char path[4096];snprintf(path,sizeof(path),"%s/%s.png",output,name);
    FILE *file=fopen(path,"wb");CHECK(file);
    png_structp p=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    png_infop info=png_create_info_struct(p);CHECK(p&&info);
    if(setjmp(png_jmpbuf(p)))abort();
    png_init_io(p,file);png_set_IHDR(p,info,960,544,8,PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p,info);
    for(int y=0;y<544;y++)png_write_row(p,pixels[y][0]);
    png_write_end(p,info);png_destroy_write_struct(&p,&info);fclose(file);
}
static void context_tests(void) {
    UiContextMenu menu={0};
    UiContextAction items[]={{"Play",NULL,10,1},{"Unavailable",NULL,11,0},{"Information",NULL,12,1}};
    UiTouchEvent touch={0};unsigned pressed=SCE_CTRL_RTRIGGER,flags=0;
    CHECK(ui_context_menu_input(&menu,items,3,&pressed,0,128,&flags,&touch)==-1 && menu.open && !pressed);
    ui_context_menu_tick(&menu);CHECK(menu.animation==1);
    pressed=SCE_CTRL_DOWN;
    CHECK(ui_context_menu_input(&menu,items,3,&pressed,0,128,&flags,&touch)==-1 && menu.cursor==1);
    pressed=SCE_CTRL_CROSS;
    CHECK(ui_context_menu_input(&menu,items,3,&pressed,0,128,&flags,&touch)==-1 && menu.open);
    pressed=SCE_CTRL_DOWN;
    CHECK(ui_context_menu_input(&menu,items,3,&pressed,0,128,&flags,&touch)==-1 && menu.cursor==2);
    pressed=SCE_CTRL_CROSS;
    CHECK(ui_context_menu_input(&menu,items,3,&pressed,0,128,&flags,&touch)==12 && !menu.open && !pressed);
    menu.open=1;pressed=SCE_CTRL_LTRIGGER;
    CHECK(ui_context_menu_input(&menu,items,3,&pressed,0,128,&flags,&touch)==-1 && !menu.open && pressed==SCE_CTRL_LTRIGGER);
    menu.open=1;pressed=0;flags=UI_TOUCH_EVENT_TAP;touch.x=640;touch.y=100;
    CHECK(ui_context_menu_input(&menu,items,3,&pressed,0,128,&flags,&touch)==10 && !menu.open && !flags);
    menu.open=1;flags=UI_TOUCH_EVENT_TAP;touch.x=40;
    CHECK(ui_context_menu_input(&menu,items,3,&pressed,0,128,&flags,&touch)==-1 && !menu.open);
}
static void availability_tests(void) {
    catalog.count=6;saved_view=starred_view=0;search[0]=0;hide_unavailable=0;
    for(unsigned i=0;i<catalog.count;i++) {
        catalog.items[i]=(VtIptvChannel){.mode=i==5?VT_IPTV_LIST:VT_IPTV_HLS};
        snprintf(catalog.items[i].url,sizeof(catalog.items[i].url),"https://example.org/%u",i);
        strcpy(catalog.items[i].title,"Channel");
    }
    const char *message="";probe_calls=0;
    availability_filter(&message);filter_catalog();
    CHECK(probe_calls==5 && probe_current==5 && result_count==3);
    CHECK(results[0]==0 && results[1]==1 && results[2]==5);
    availability_filter(&message);filter_catalog();CHECK(result_count==6 && probe_calls==5);
    probe_calls=0;probe_cancel_after=4;availability_filter(&message);filter_catalog();
    CHECK(probe_calls==4 && probe_current==3 && result_count==5);
    availability_filter(&message);probe_cancel_after=0;probe_connected=0;probe_calls=0;
    availability_filter(&message);filter_catalog();CHECK(!probe_calls && result_count==6);
    availability_filter(&message);probe_connected=1;
    VtIptvChannel playing=catalog.items[0];playing.starred=0;stored_channel=playing;
    CHECK(player_favorite(&playing,1)==1 && stored_channel.starred && catalog.items[0].starred);
    stored_result=-1;CHECK(player_favorite(&playing,0)==1); /* Query is memory-only. */
    CHECK(player_favorite(&playing,1)<0 && playing.starred);stored_result=0;
    write_failure=1;CHECK(player_favorite(&playing,1)<0 && playing.starred);write_failure=0;
    CHECK(!player_favorite(&playing,1) && !catalog.items[0].starred);
    memset(&catalog,0,sizeof(catalog));memset(&stored_channel,0,sizeof(stored_channel));
    filter_dirty=1;filter_catalog();
}
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"Usage: ui_test <Inter-Medium.ttf> <existing output directory>\n");return 2;}
    output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
    context_tests();
    availability_tests();
    CHECK(UI_CONTENT_X+3*UI_GRID_CARD_W+2*9==UI_CONTENT_RIGHT);
    CHECK(110+2*UI_GRID_CARD_H+UI_CONTENT_GAP<=UI_CONTENT_BOTTOM);
    CHECK(UI_GRID_TITLE_Y-UI_FONT_SMALL>6+UI_GRID_THUMB_H);
    CHECK(UI_GRID_DETAIL_Y+3<UI_GRID_CARD_H);
    char clean[16];
    label_copy(clean,sizeof(clean),"https://user:secret-token@example.org/file.mkv?secret-token#fragment");
    CHECK(!strcmp(clean,"file.mkv"));
    label_copy(clean,sizeof(clean),"https://user:secret-token@example.org?secret-token");CHECK(!clean[0]);
    label_copy(clean,5,"日本語");CHECK(!strcmp(clean,"日"));
    label_copy(clean,sizeof(clean),"Two\nlines");CHECK(!strcmp(clean,"Two lines"));
    ui_brand_draw_breadcrumbs("IPTV","My channel list","News channel");
    CHECK(strstr(trail_text,"IPTV / My channel list / News channel"));
    ui_brand_draw_breadcrumbs("Podcast","A very long channel title that cannot possibly fit alongside its current episode","Episode 42");
    CHECK(strstr(trail_text,"Episode 42") && strstr(trail_text,"..."));
    ui_brand_draw_breadcrumbs("Network","Home server","Very long directory / Another very long directory / Final folder");
    CHECK(strstr(trail_text,"Final folder"));
    ui_brand_draw_breadcrumbs("日本語","Bibliothèque / Immagini delle vacanze","旅行写真 — très longue description de cette photographie très détaillée");
    png("breadcrumb-long");
    strcpy(link,"https://example.org/live/channel.m3u8?token=secret-token");
    for(language=0;language<2;language++) {
        frame=0;CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);CHECK(frame==1);
        png(language?"iptv-it":"iptv-en");
        unsigned script[]={0,SCE_CTRL_START,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
        memcpy(input_script,script,sizeof(script));script_count=6;capture_frame=2;frame=0;
        CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
        memcpy(pixels,first_pixels,sizeof(pixels));png(language?"iptv-add-it":"iptv-add-en");
        script_count=capture_frame=0;
        catalog.count=12;
        for(unsigned i=0;i<catalog.count;i++){
            snprintf(catalog.items[i].title,sizeof(catalog.items[i].title),"%s",i==0?"News channel":i==1?"On-demand movie":"DASH channel");
            catalog.items[i].mode=i==0?VT_IPTV_HLS:i==1?VT_IPTV_FILE:VT_IPTV_DASH;
            catalog.items[i].starred=i==0;
            strcpy(catalog.items[i].artwork_path,"fixture-square.png");
        }
        frame=0;online_case=1;CHECK(ui_iptv_screen()==UI_SECTION_SETTINGS);
        memcpy(pixels,first_pixels,sizeof(pixels));png(language?"iptv-channels-it":"iptv-channels-en");
        CHECK(catalog_visible==6);
        mini_top=544-UI_MINI_PLAYER_HEIGHT;frame=0;
        CHECK(ui_iptv_screen()==UI_SECTION_SETTINGS && catalog_visible==4);
        memcpy(pixels,first_pixels,sizeof(pixels));png(language?"iptv-channels-it-mini":"iptv-channels-en-mini");
        mini_top=544;
        UiContextMenu menu={.open=1,.animation=1};UiContextAction items[8];
        int count=options_for_page(items);CHECK(count<=8);
        ui_context_menu_draw(&menu,TR(UI_OPTIONS),items,count);png(language?"iptv-options-it":"iptv-options-en");
        online_case=0;catalog.count=0;
        UiSectionsSidebar sidebar;live_slots=1;ui_sections_sidebar_init(&sidebar,UI_SECTION_IPTV);
        sidebar.open=1;sidebar.animation=1;sidebar.cursor=sidebar.focus_cursor=UI_SECTION_INFO+2;
        CHECK((sidebar_scroll_max(&sidebar) > 0) == sidebar_vertical());
        UiThemeRect focused=sidebar_slot_rect(&sidebar,sidebar.cursor);
        CHECK(focused.y>=sidebar_view_top()&&focused.y+focused.h<=sidebar_view_bottom());
        ui_sections_sidebar_draw(&sidebar);png(language?"drawer-it":"drawer-en");
        live_slots=0;
    }
    language=0;
    UiSectionsSidebar side;
    for(unsigned available=0;available<2;available++) {
        live_slots=available;ui_sections_sidebar_init(&side,UI_SECTION_LOCAL_MEDIA);
        for(int target=0;target<UI_SECTION_COUNT;target++) {
            /* Opening L1 focuses the active section; presentation order is
             * independent of the stable IDs after adding personal services. */
            ui_sections_sidebar_init(&side,target);
            side.active=UI_SECTION_NONE;side.open=1;
            unsigned buttons=SCE_CTRL_CROSS;
            CHECK(ui_sections_sidebar_handle_buttons(&side,&buttons,0,128)==target);
        }
    }
    live_slots=0;
    const char *message=NULL;
    strcpy(link,"file:///private/secret");CHECK(play_link(&message)==UI_SECTION_NONE&&!created&&!stops);
    strcpy(link,"https://example.org/live.m3u8");next_result=-1;next_event=VT_IPTV_DISCONTINUITY;
    CHECK(play_link(&message)==UI_SECTION_NONE&&created==1&&destroyed==1&&saved==1);
    CHECK(message==TR(IPTV_CHANGED));
    CHECK(playback_notices==1 && playback_notice==TR(IPTV_CHANGED));
    plays=created=destroyed=0;next_result=0;next_event=0;reconnect=1;
    CHECK(play_link(&message)==UI_SECTION_NONE&&created==2&&destroyed==2);
    CHECK(stops==2); /* One synchronous previous-player stop per Play action. */
    reconnect=0;next_result=VT_HW_PLAYER_ACTION_SECTION_BASE+UI_SECTION_NETWORK;
    CHECK(play_link(&message)==UI_SECTION_NETWORK&&created==3&&destroyed==3);
    mode=VT_IPTV_DASH;CHECK(play_link(&message)==UI_SECTION_NETWORK&&dash_selected==1&&created==4&&destroyed==4);
    mode=VT_IPTV_HLS;
    VtIptvChannel item={0};
    strcpy(item.url,"https://example.org/list.m3u");strcpy(item.title,"My list");item.mode=VT_IPTV_LIST;
    CHECK(save_link(&item)==0&&!stored_channel.starred);
    CHECK(toggle_star(&item)==0&&stored_channel.starred);
    strcpy(stored_channel.logo,"https://example.org/logo.png");
    item.starred=0;strcpy(item.title,"Renamed list");
    CHECK(save_link(&item)==0&&stored_channel.starred);
    CHECK(!strcmp(stored_channel.title,"Renamed list")&&!strcmp(item.logo,stored_channel.logo));
    CHECK(toggle_star(&item)==0&&!stored_channel.starred);
    stored_result=-1;strcpy(item.title,"Must not overwrite");
    CHECK(save_link(&item)<0&&!strcmp(stored_channel.title,"Renamed list"));
    stored_result=0;item=stored_channel;CHECK(!toggle_star(&item));CHECK(item.starred);
    strcpy(item.user_agent,"HbbTV/1.6.1");CHECK(!save_link(&item));
    current_list=item;strcpy(link,item.url);
    rename_input=NULL;CHECK(!rename_saved_link(&item,&message));
    rename_input="  \t ";CHECK(rename_saved_link(&item,&message)<0&&message==TR(IPTV_NAME_INVALID));
    CHECK(!strcmp(item.title,"Renamed list"));
    rename_input="Line\nbreak";CHECK(rename_saved_link(&item,&message)<0);
    rename_input="  La mia TV 日本語  ";CHECK(rename_saved_link(&item,&message)==1);
    CHECK(!strcmp(item.title,"La mia TV 日本語")&&!strcmp(item.title,stored_channel.title));
    CHECK(!strcmp(current_list.title,item.title)&&!strcmp(channel_title,item.title));
    CHECK(!strcmp(item.user_agent,"HbbTV/1.6.1")&&item.mode==VT_IPTV_LIST);
    CHECK(!strcmp(item.logo,"https://example.org/logo.png"));
    CHECK(item.starred&&stored_channel.starred);
    write_failure=1;rename_input="Do not commit";CHECK(rename_saved_link(&item,&message)<0);
    CHECK(!strcmp(item.title,"La mia TV 日本語")&&!strcmp(stored_channel.title,item.title));
    write_failure=0;rename_input=NULL;
    catalog.count=1;catalog.items[0]=item;catalog_focus=0;saved_view=1;
    UiContextAction actions[8];int count=options_for_page(actions),renames=0,deletes=0;
    for(int i=0;i<count;i++){CHECK(actions[i].action!=2&&actions[i].action!=7);if(actions[i].action==14)renames++;if(actions[i].action==15)deletes++;}
    CHECK(renames==1&&deletes==1&&count<=8);
    write_failure=1;int old_star=item.starred;CHECK(toggle_star(&item)<0&&item.starred==old_star);write_failure=0;
    /* Cancel by default, held opening X, explicit confirmation and write failure. */
    for(language=0;language<2;language++) {
        frame=0;online_case=0;CHECK(!remove_saved_link(&item,&message)&&!removed);
        png(language?"iptv-remove-it":"iptv-remove-en");
    }
    language=0;
    unsigned cancel_script[]={SCE_CTRL_CROSS,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS};
    memcpy(input_script,cancel_script,sizeof(cancel_script));script_count=4;frame=0;
    CHECK(!remove_saved_link(&item,&message)&&frame==3&&!removed);
    unsigned delete_script[]={SCE_CTRL_CROSS,0,SCE_CTRL_RIGHT,0,SCE_CTRL_CROSS};
    memcpy(input_script,delete_script,sizeof(delete_script));script_count=5;frame=0;write_failure=1;
    CHECK(remove_saved_link(&item,&message)<0 && !removed && stored_channel.url[0]);
    CHECK(message==TR(IPTV_DELETE_FAILED));
    frame=0;write_failure=0;CHECK(remove_saved_link(&item,&message)==1&&removed==1&&!stored_channel.url[0]);
    CHECK(message==TR(IPTV_DELETED)&&!current_list.url[0]);
    stored_channel=item;frame=0;script_count=0;tap_frame=0;tap_x=600;tap_y=372;
    CHECK(remove_saved_link(&item,&message)==1&&removed==2);tap_frame=-1;
    strcpy(link,"https://example.org/old.m3u8");strcpy(channel_agent,"old-agent");strcpy(channel_title,"Old channel");
    new_link();CHECK(!link[0]&&!channel_title[0]&&!channel_agent[0]&&!catalog.count&&!saved_view);
    CHECK(!accept_link(" \t https://example.org/new.m3u \r\n")&&mode==VT_IPTV_LIST);
    CHECK(!strcmp(link,"https://example.org/new.m3u"));
    CHECK(accept_link("https://user:secret@example.org/list.m3u")<0);
    CHECK(!strcmp(link,"https://example.org/new.m3u"));
    CHECK(accept_link("https://example.org/line\nbreak")<0);
    current_list=item;saved_view=0;catalog.count=1;catalog.items[0]=item;catalog_focus=0;
    count=options_for_page(actions);int saves=0;deletes=0;
    for(int i=0;i<count;i++){saves+=actions[i].action==16;deletes+=actions[i].action==15;}
    CHECK(saves==1&&!deletes&&count<=8);saved_view=1;
    CHECK(failure_detail(VT_IPTV_DNS)==TR(IPTV_DNS_ERROR));
    CHECK(failure_detail(VT_IPTV_ACCESS)==TR(IPTV_ACCESS_ERROR));
    CHECK(failure_detail(VT_IPTV_NOT_FOUND)==TR(IPTV_NOT_FOUND_ERROR));
    CHECK(failure_detail(VT_IPTV_TIMEOUT)==TR(IPTV_TIMEOUT_ERROR));
    CHECK(failure_detail(VT_IPTV_TLS)==TR(IPTV_TLS_ERROR));
    CHECK(failure_detail(VT_IPTV_REDIRECT)==TR(IPTV_REDIRECT_ERROR));
    unsigned before_notice=playback_notices;
    next_result=-5;next_event=VT_IPTV_ACCESS;mode=VT_IPTV_HLS;
    strcpy(link,"https://example.org/denied.m3u8");
    CHECK(play_link(&message)==UI_SECTION_NONE);
    CHECK(playback_notices==before_notice+1&&playback_notice==TR(IPTV_ACCESS_ERROR));
    CHECK(catalog.count==1&&catalog_focus==0&&saved_view);
    next_result=next_event=0;
    /* Filtering keeps stable source indices, so Play/Star/Rename/Delete do
     * not accidentally act on an unrelated entry at the same grid position. */
    catalog.count=5;saved_view=1;catalog_focus=catalog_top=0;
    for(unsigned i=0;i<catalog.count;i++) {
        catalog.items[i]=(VtIptvChannel){.mode=VT_IPTV_HLS};
        snprintf(catalog.items[i].url,sizeof(catalog.items[i].url),"https://example.org/channel%u.m3u8",i);
        snprintf(catalog.items[i].title,sizeof(catalog.items[i].title),"%s",i==1?"RaiNews":i==4?"Rai News 24":"Other channel");
    }
    strcpy(search,"rai news");filter_dirty=1;filter_catalog();
    CHECK(result_count==2 && results[0]==1 && results[1]==4 && selected_channel()==&catalog.items[1]);
    catalog_focus=1;CHECK(selected_channel()==&catalog.items[4]);
    CHECK(!toggle_star(selected_channel()) && !strcmp(stored_channel.url,catalog.items[4].url));
    rename_input=NULL;search_catalog();CHECK(!strcmp(search,"rai news") && catalog_focus==1);
    catalog_focus=0;
    unsigned search_script[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
    memcpy(input_script,search_script,sizeof(search_script));script_count=6;capture_frame=2;frame=0;
    CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
    CHECK(!strcmp(last_stream_url,catalog.items[1].url));
    CHECK(!strcmp(search,"rai news")); /* Circle leaves the level; it does not consume Back clearing a filter. */
    memcpy(pixels,first_pixels,sizeof(pixels));png("iptv-search-results");
    const char *empty_message="";
    for(language=0;language<2;language++) {
        strcpy(search,"no match whatsoever");filter_dirty=1;filter_catalog();
        CHECK(!result_count && !selected_channel());
        count=options_for_page(actions);
        for(int i=0;i<count;i++)if(actions[i].action==10 || actions[i].action==14 || actions[i].action==15)CHECK(!actions[i].enabled);
        CHECK(!remove_saved_link(selected_channel(),&empty_message));
        unsigned empty_script[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
        memcpy(input_script,empty_script,sizeof(empty_script));script_count=6;frame=0;capture_frame=0;
        CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
        memcpy(pixels,first_pixels,sizeof(pixels));png(language?"iptv-search-empty-it":"iptv-search-empty-en");
    }
    language=0;script_count=capture_frame=0;search[0]=saved_search[0]=0;
    CHECK(failure_detail(VT_IPTV_ENCRYPTED)==TR(IPTV_ENCRYPTED_ERROR));
    CHECK(failure_detail(VT_IPTV_RENDITIONS)==TR(IPTV_RENDITIONS_ERROR));
    CHECK(failure_detail(VT_IPTV_VARIANT_LIMIT)==TR(IPTV_VARIANT_LIMIT_ERROR));
    FT_Done_Face(face);FT_Done_FreeType(ft);
    printf("IPTV UI/navigation/layout: %u assertions passed\n",checks);
    return 0;
}
