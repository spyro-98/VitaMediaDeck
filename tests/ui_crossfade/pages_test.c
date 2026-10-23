/* Native page composition with host fonts, platform/preview substitutes.
 * PNGs validate layout, not Vita rendering or the subtitle engine. */
#define main iptv_ui_main
#include "../iptv/ui_test.c"
#undef main
#include "../../src/ui/settings_screen.c"
#include "../../src/ui/text_reader.c"
#include "../../src/ui/local_media_actions.c"
#undef TAB_Y
#undef TAB_H
#include "../../src/ui/about_screen.c"
static int settings_writes;
#define PREF(name,value) int vt_preferences_##name(void){return value;} int vt_preferences_set_##name(int n){(void)n;settings_writes++;return 0;}
static int theme_write_failure,theme_prepares;
int vt_preferences_interface_theme(void){return ui_theme_id();}
int vt_preferences_set_interface_theme(int theme){(void)theme;settings_writes++;return theme_write_failure?-1:0;}
void ui_runtime_prepare_theme(void){theme_prepares++;}
PREF(video_decoder,0) PREF(fill_screen,0) PREF(loop_enabled,0)
PREF(music_keep_display_awake,1) PREF(player_swap_shoulders,0)
PREF(language,1) PREF(player_status_always_visible,0) PREF(clock_source,1)
PREF(disk_logs_enabled,0) PREF(remember_network_passwords,0)
PREF(subtitle_font,0) PREF(subtitle_text_color,0) PREF(subtitle_border_color,0)
PREF(subtitle_background_color,0) PREF(subtitle_size,1) PREF(subtitle_max_width,2)
PREF(subtitle_min_rows,1) PREF(subtitle_max_rows,4) PREF(subtitle_position,0)
int vt_preferences_set_reduce_motion(int n){(void)n;return 0;}
int ui_home_tabs_configure(void){return 0;}
int vt_network_credentials_clear(void){return 0;}
int vt_i18n_set_language(int n){(void)n;return 0;}
void ui_brand_draw_header_placeholder(const char *p,const char*t){(void)p;ui_brand_draw_header(t);}
void ui_brand_draw_status_indicators(void){}
vita2d_texture *ui_runtime_logo(void){return NULL;}
int ui_log_viewer_screen(void){return UI_SECTION_NONE;}
int ui_runtime_is_ready(void){return 1;}
int scePowerGetArmClockFrequency(void){return 444;}
int scePowerGetGpuClockFrequency(void){return 222;}
int scePowerGetBusClockFrequency(void){return 222;}
int scePowerGetGpuXbarClockFrequency(void){return 111;}
void vita2d_draw_fill_circle(float x,float y,float r,unsigned c){for(int dy=-(int)r;dy<=r;dy++)for(int dx=-(int)r;dx<=r;dx++)if(dx*dx+dy*dy<=r*r)pixel(x+dx,y+dy,c,255);}
void ui_subtitle_draw(const char *text,float x,float y,float w,float h){(void)text;ui_text_line(x+12,y+h-18,w-24,"Subtitle renderer substituted on host",VT_THEME_TEXT,UI_FONT_SMALL);}
int main(int argc,char **argv) {
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 for(language=0;language<2;language++)for(int mini=0;mini<2;mini++){
  mini_top=mini?544-UI_MINI_PLAYER_HEIGHT:544;
  for(int tab=0;tab<TAB_COUNT;tab++)for(int owner=0;owner<2;owner++){
   UiSectionsSidebar sidebar;ui_sections_sidebar_init(&sidebar,UI_SECTION_SETTINGS);
   UiFocusMotion focus={0};ui_focus_motion_tick(&focus,owner?18+tab*186:18,owner?62:118,owner?178:tab==TAB_SUBTITLES?386:924,owner?40:56);
   frame=0;draw_screen(tab,0,owner,&focus,&sidebar);
   char name[80];snprintf(name,sizeof(name),"settings-%d-%s-%s%s",tab,owner?"tabs":"rows",language?"it":"en",mini?"-mini":"");png(name);
  }
  GuideChapter chapters[8];const char *guide=vt_i18n_str(VT_STR_SETTINGS_CONTROLS_REFERENCE_IT);
  CHECK(guide_chapters(guide,chapters,8)==7);
  for(int i=0;i<7;i++)CHECK(chapters[i].length>0 && chapters[i].body>=guide && chapters[i].body+chapters[i].length<=guide+strlen(guide));
  CHECK(guide_chapters("No headings here",chapters,8)==0);
  script_count=4;input_script[0]=0;input_script[1]=SCE_CTRL_RIGHT;input_script[2]=0;input_script[3]=SCE_CTRL_CIRCLE;
  frame=0;capture_frame=2;CHECK(ui_controls_guide_run(vt_i18n_str(VT_STR_SECTIONS_GUIDE),guide)==UI_SECTION_NONE);
  memcpy(pixels,first_pixels,sizeof(pixels));char name[80];snprintf(name,sizeof(name),"guide-%s%s",language?"it":"en",mini?"-mini":"");png(name);
  frame=0;capture_frame=0;CHECK(ui_about_screen(0)==UI_SECTION_NONE);
  memcpy(pixels,first_pixels,sizeof(pixels));snprintf(name,sizeof(name),"about-%s%s",language?"it":"en",mini?"-mini":"");png(name);
 }
 /* Cross on a tab only enters its rows; it must not also toggle a setting. */
 frame=0;script_count=3;input_script[0]=0;input_script[1]=SCE_CTRL_CROSS;input_script[2]=SCE_CTRL_CIRCLE;
 CHECK(ui_settings_screen()==UI_SECTION_NONE && settings_writes==0);
 frame=0;script_count=5;input_script[0]=0;input_script[1]=SCE_CTRL_CROSS;input_script[2]=0;input_script[3]=SCE_CTRL_CROSS;input_script[4]=SCE_CTRL_CIRCLE;
 CHECK(ui_settings_screen()==UI_SECTION_NONE && settings_writes==1);
 VtLocalMediaItem item={0};strcpy(item.name,"A long image title to check the modal layout");strcpy(item.path,"ux0:downloads/Folder/Example image.png");
 frame=0;capture_frame=0;script_count=2;input_script[0]=0;input_script[1]=SCE_CTRL_CROSS;
 CHECK(ui_local_media_confirm_delete(&item)==0);memcpy(pixels,first_pixels,sizeof(pixels));png("delete-default-cancel");
 frame=0;script_count=4;input_script[0]=0;input_script[1]=SCE_CTRL_RIGHT;input_script[2]=0;input_script[3]=SCE_CTRL_CROSS;
 CHECK(ui_local_media_confirm_delete(&item)==1);
 frame=0;script_count=2;input_script[0]=0;input_script[1]=SCE_CTRL_CIRCLE;
 CHECK(ui_local_media_actions(&item)==0);memcpy(pixels,first_pixels,sizeof(pixels));png("file-actions");
 /* Real settings action: failed persistence must not change the live palette. */
 for(int i=1;i<=VT_UI_THEME_COUNT;i++){
  CHECK(apply_row(TAB_INTERFACE,4,1)==0);
  CHECK(ui_theme_id()==i%VT_UI_THEME_COUNT);
 }
 CHECK(theme_prepares==VT_UI_THEME_COUNT);theme_write_failure=1;
 CHECK(apply_row(TAB_INTERFACE,4,1)<0&&ui_theme_id()==VT_UI_THEME_LEGACY&&theme_prepares==VT_UI_THEME_COUNT);
 theme_write_failure=0;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(int mini=0;mini<2;mini++){
  ui_theme_apply(theme);mini_top=mini?472:544;
  UiSectionsSidebar sidebar;ui_sections_sidebar_init(&sidebar,UI_SECTION_SETTINGS);
  UiFocusMotion focus={0};ui_focus_motion_tick(&focus,18,118+4*64,924,56);
  frame=0;draw_screen(TAB_INTERFACE,4,0,&focus,&sidebar);
  char name[96];snprintf(name,sizeof(name),"theme-settings-%d-%s%s",theme,language?"it":"en",mini?"-mini":"");png(name);
 }
 ui_theme_apply(VT_UI_THEME_LEGACY);
 FT_Done_Face(face);FT_Done_FreeType(ft);printf("Crossfade native settings/guide/about/actions: %u checks passed\n",checks);
}
