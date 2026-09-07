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
#include "../../src/ui/iptv_screen.c"
#include "../../src/ui/sections_sidebar.c"
#include "../../src/ui/components.c"

static unsigned checks, frame, language, live_slots, created, destroyed, plays, stops, saved;
static int next_result, next_event, reconnect;
static FT_Library ft;
static FT_Face face;
static unsigned char pixels[544][960][3];
static const char *output;
#define CHECK(x) do { ++checks; assert(x); } while (0)
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
    unsigned alpha=(color>>24)*coverage/255;
    for(int c=0;c<3;c++)pixels[y][x][c]=(unsigned char)(
        (((color>>(c*8))&255)*alpha+pixels[y][x][c]*(255-alpha))/255);
}
int ui_font_draw_text(vita2d_font *font,int x,int y,unsigned color,unsigned size,const char *text) {
    int width=ui_font_text_width(font,size,text);
    if (x<0||y<(int)size||x+width>960||y>544)
        fprintf(stderr,"Text bounds: x=%d y=%d width=%d size=%u text=%s\n",x,y,width,size,text);
    CHECK(x>=0&&y>=(int)size&&x+width<=960&&y<=544);
    CHECK(!strstr(text,"secret-token"));
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
    for(int row=(int)y;row<(int)ceilf(y+h);row++)for(int col=(int)x;col<(int)ceilf(x+w);col++)
        pixel(col,row,color,255);
}
void vita2d_start_drawing(void) {}
void vita2d_end_drawing(void) {}
void vita2d_wait_rendering_done(void) {}
void vita2d_clear_screen(void) { memset(pixels,0,sizeof(pixels)); }
void vita2d_swap_buffers(void) { frame++; }
uint64_t sceKernelGetProcessTimeWide(void) { return frame*16667ULL+1; }
int sceKernelDelayThread(unsigned us) { (void)us;return 0; }
int sceCtrlSetSamplingMode(int mode) { (void)mode;return 0; }
int sceCtrlPeekBufferPositive(int port,SceCtrlData *pad,int count) {
    (void)port;(void)count;*pad=(SceCtrlData){.lx=128,.ly=128,.rx=128,.ry=128,
        .buttons=frame?SCE_CTRL_CIRCLE:0};return 1;
}
unsigned ui_touch_poll(UiTouchEvent *event) { memset(event,0,sizeof(*event));return 0; }
void ui_touch_reset(void) {}
int ui_touch_hit_rect(int x,int y,int rx,int ry,int rw,int rh) {return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh;}
int ui_brand_sections_hit(unsigned flags,int x,int y) { (void)flags;(void)x;(void)y;return 0; }
void ui_brand_draw_header(const char *title) {
    vita2d_draw_rectangle(0,0,960,54,VT_THEME_SURFACE);
    ui_font_draw_text(NULL,30,35,VT_THEME_TEXT,20,"VitaMediaDeck");
    ui_font_draw_text(NULL,305,35,VT_THEME_TEXT,20,title);
}
void ui_mini_player_pump(void) {}
void ui_mini_player_draw(void) {}
int ui_mini_player_handle_buttons(unsigned *buttons) { (void)buttons;return 0; }
int ui_mini_player_handle_touch(unsigned flags,const UiTouchEvent *event) { (void)flags;(void)event;return 0; }
int ui_mini_player_input_locked(void) { return 0; }
int vt_preferences_reduce_motion(void) { return 1; }
int vt_background_playback_can_resume_fullscreen(void) { return live_slots; }
int vt_download_background_active(void) { return live_slots; }
void ui_home_tab_set(int tab) { (void)tab; }
void ui_online_audio_set_home(int favorites) { (void)favorites; }
void vt_video_thumbnail_prepare_playback(void) {}
void vt_background_playback_set_fullscreen_resume(VtBackgroundFullscreenResume fn,void *ctx) { CHECK(!fn&&!ctx); }
void vt_background_playback_stop(void) { stops++; }
void log_printf(const char *format,...) { CHECK(!strstr(format,"http")); }
int log_save(const char *path) { CHECK(!strcmp(path,VITAMEDIADECK_SESSION_LOG_PATH));saved++;return 0; }
int ui_text_input(const char *title,const char *initial,char *out,size_t capacity) {
    (void)title;(void)initial;(void)out;(void)capacity;return 0;
}
VtIptvSession *vt_iptv_session_create(const char *url,const VtIptvSourceConfig *config) {
    (void)config;CHECK(strstr(url,"https://"));created++;return (void *)(uintptr_t)created;
}
void vt_iptv_session_factory(VtIptvSession *s,VtDecoderStreamFactory *f) { memset(f,0,sizeof(*f));f->opaque=s; }
int vt_hw_player_screen_run_live(const VtHwPlayerScreenSource *s,VtIptvSession *session) {
    CHECK(s->stream.opaque==session&&!s->allow_minimize&&!s->history_id&&s->authenticated_remote);
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
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"Usage: ui_test <Inter-Medium.ttf> <existing output directory>\n");return 2;}
    output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
    strcpy(link,"https://example.org/live/channel.m3u8?token=secret-token");
    for(language=0;language<2;language++) {
        frame=0;CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);CHECK(frame==1);
        png(language?"iptv-it":"iptv-en");
        UiSectionsSidebar sidebar;live_slots=1;ui_sections_sidebar_init(&sidebar,UI_SECTION_IPTV);
        sidebar.open=1;sidebar.animation=1;sidebar.cursor=sidebar.focus_cursor=UI_SECTION_INFO+2;
        CHECK(ITEM_Y+(UI_SECTION_COUNT+1)*ITEM_STEP+ITEM_H<504);
        ui_sections_sidebar_draw(&sidebar);png(language?"drawer-it":"drawer-en");
        live_slots=0;
    }
    language=0;
    UiSectionsSidebar side;
    for(unsigned available=0;available<2;available++) {
        live_slots=available;ui_sections_sidebar_init(&side,UI_SECTION_LOCAL_MEDIA);
        for(int target=0;target<UI_SECTION_COUNT;target++) {
            side.active=UI_SECTION_NONE;side.open=1;side.cursor=target+2*available;
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
    plays=created=destroyed=0;next_result=0;next_event=0;reconnect=1;
    CHECK(play_link(&message)==UI_SECTION_NONE&&created==2&&destroyed==2);
    CHECK(stops==2); /* One synchronous previous-player stop per Play action. */
    reconnect=0;next_result=VT_HW_PLAYER_ACTION_SECTION_BASE+UI_SECTION_NETWORK;
    CHECK(play_link(&message)==UI_SECTION_NETWORK&&created==3&&destroyed==3);
    FT_Done_Face(face);FT_Done_FreeType(ft);
    printf("IPTV UI/navigation/layout: %u assertions passed\n",checks);
}
