/* Production utility views + real brand/status. Camera and I/O are substituted;
 * the scanner's borrowed texture is checked pixel-for-pixel, never decoded here. */
#define VMD_CHROME_HELPERS_ONLY
#include "../ui_chrome/test.c"
void vita2d_draw_texture_part_scale(const vita2d_texture *,float,float,float,float,float,float,float,float);
#include "../../src/ui/qr_view.c"
#include "../../src/ui/log_viewer.c"
void ui_online_audio_set_home(int n){(void)n;}
int vt_preferences_disk_logs_enabled(void){return 0;}
int sceIoOpen(const char *p,int f,int m){(void)p;(void)f;(void)m;return -1;}
int sceIoClose(int f){(void)f;return 0;}
int sceIoRead(int f,void *p,unsigned n){(void)f;(void)p;(void)n;return -1;}
SceOff sceIoLseek(int f,SceOff off,int w){(void)f;(void)off;(void)w;return -1;}
int ui_font_draw_text_centered(vita2d_font *font,int cx,int y,int max,unsigned c,unsigned size,const char *text){
 char fitted[512];int w=ui_font_fit_text(font,size,text,fitted,sizeof(fitted),max);
 return ui_font_draw_text(font,cx-w/2,y,c,size,fitted);
}
static unsigned preview_calls;
static unsigned camera_color(int x,int y){return RGBA8(30+x%190,30+y%190,80,255);}
void vita2d_draw_texture_part_scale(const vita2d_texture *t,float x,float y,float tx,float ty,float tw,float th,float sx,float sy){
 CHECK(t&&(int)x==18&&(int)y==62&&tx==80&&ty==0&&tw==480&&th==480);
 CHECK(fabsf(sx-444.f/480)<.0001f&&sx==sy);preview_calls++;
 for(int row=0;row<444;row++)for(int col=0;col<444;col++)pixel(x+col,y+row,camera_color(col,row),255);
}
static void camera_intact(void){
 for(int y=0;y<444;y++)for(int x=0;x<444;x++){
  unsigned c=camera_color(x,y);const unsigned char *p=pixels[62+y][18+x];
  CHECK(p[0]==(c&255)&&p[1]==((c>>8)&255)&&p[2]==((c>>16)&255));
 }
}
static unsigned long long digest(void){
 unsigned long long h=1469598103934665603ULL;const unsigned char *p=(void*)pixels;
 for(size_t i=0;i<sizeof(pixels);i++)h=(h^p[i])*1099511628211ULL;
 return h;
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 FT_Face inter=face,mono;CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));
 unsigned long long qr_digests[VT_UI_THEME_COUNT]={0};unsigned rendered=0;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;
  for(int reduced=0;reduced<2;reduced++)for(int state=0;state<UI_QR_STATE_COUNT;state++){
   host_reduced_motion=reduced;frame=120;clipped=0;vita2d_clear_screen();
   UiQrStatus status={(UiQrState)state,4294967295U,4294967295U};
   unsigned calls=preview_calls;ui_qr_view_draw(state?(vita2d_texture*)&icon:NULL,&status);
   CHECK(preview_calls==calls+(state!=0));if(state)camera_intact();rendered++;
   if(language&&reduced&&state==UI_QR_SEARCHING){char name[80];snprintf(name,sizeof(name),"qr-%d",theme);png(name);qr_digests[theme]=digest();}
   if(language&&reduced&&state==UI_QR_INVALID_PROTOCOL){char name[80];snprintf(name,sizeof(name),"qr-rejected-%d",theme);png(name);}
  }
  host_reduced_motion=1;frame=120;vita2d_clear_screen();
  ui_brand_draw_search_backdrop_editing_label("https://example.test/stream.m3u8",7,1,
   vt_i18n_str(VT_STR_NETWORK_URL_PROMPT),vt_i18n_str(VT_STR_BRAND_TEXT_INPUT_HINT));
  if(language){char name[80];snprintf(name,sizeof(name),"keyboard-%d",theme);png(name);}
  /* A stationary editor must not replay item entrances or pulse when reduced. */
  unsigned long long stable=digest();frame+=12;vita2d_clear_screen();
  ui_brand_draw_search_backdrop_editing_label("https://example.test/stream.m3u8",7,1,
   vt_i18n_str(VT_STR_NETWORK_URL_PROMPT),vt_i18n_str(VT_STR_BRAND_TEXT_INPUT_HINT));
  CHECK(digest()==stable);
  UiSectionsSidebar side;ui_sections_sidebar_init(&side,UI_SECTION_INFO);UiFocusMotion focus={0};
  ui_focus_motion_tick(&focus,36,LOG_LIST_Y,888,48);
  draw_log_screen(0,0,0,&focus,&side);
  strcpy(g_log_text,"Session log\nPlayer ready\nTheme active\n");index_log_lines(0);CHECK(g_log_line_count==3);
  draw_log_screen(0,1,0,&focus,&side);
  if(language){char name[80];snprintf(name,sizeof(name),"log-%d",theme);png(name);}
 }
 for(int i=0;i<VT_UI_THEME_COUNT;i++)for(int j=i+1;j<VT_UI_THEME_COUNT;j++)CHECK(qr_digests[i]!=qr_digests[j]);
 CHECK(ui_touch_hit_rect(500,480,UI_QR_CANCEL_X,UI_QR_CANCEL_Y,UI_QR_CANCEL_W,UI_QR_CANCEL_H));
 CHECK(!ui_touch_hit_rect(240,280,UI_QR_CANCEL_X,UI_QR_CANCEL_Y,UI_QR_CANCEL_W,UI_QR_CANCEL_H));
 FT_Done_Face(inter);FT_Done_Face(mono);FT_Done_FreeType(ft);
 printf("Native utility coverage: %u checks, %u QR states; 8 distinct themes, IT/EN, reduced "
        "motion, full camera crop intact, text editor and logs passed.\n",
        checks, rendered);
}
