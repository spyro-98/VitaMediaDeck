/* Native renderer, host runtime and explicitly synthetic library data. */
#define VMD_AUDIO_TEST_LIBRARY
#include "audio_test.c"
#undef VMD_AUDIO_TEST_LIBRARY
#include "ui/home_services.h"
static void game_frame(int theme,int time,int bottom) {
 frame=time;draw_primitives=0;vita2d_clear_screen();
 ui_chrome_background(VT_THEME_BG,VT_THEME_SIGNAL_BRIGHT);
 vita2d_draw_rectangle(0,0,960,54,VT_THEME_SURFACE);
 ui_text_line(18,32,650,"VitaMediaDeck",VT_THEME_TEXT,UI_FONT_BODY);
 const char *labels[]={"Libreria","Video locali","Audio locale","Radio","Podcast","Immagini"};
 UiFocusMotion motion={0};ui_theme_home_menu(theme,2,labels,NULL,1,&motion);
 ui_theme_home_heading("Audio locale",vt_i18n_str(VT_STR_HOME_NAVIDROME_DETAIL),bottom);
 ui_theme_home_preview(NULL,"Navidrome",bottom,VT_LOCAL_MEDIA_SOURCE_NAVIDROME);
 UiThemeHomeLayout l=ui_theme_home_layout(theme,bottom);
 for(int i=0;i<l.rows;i++)ui_theme_home_row(i,i,i==0?"Navidrome":i%2?"Memory card / Music":"USB / Music",i==0?vt_i18n_str(VT_STR_HOME_NAVIDROME_DETAIL):"Cartella musicale",i==0,0,bottom);
 ui_theme_home_footer(vt_i18n_str(VT_STR_HOME_NAVIDROME_DETAIL),bottom);ui_theme_home_position(0,12,bottom);
 if(theme>=VT_UI_THEME_APERTURE)ui_theme_home_service(0,bottom,VT_LOCAL_MEDIA_SOURCE_NAVIDROME);
 if(bottom<UI_CONTENT_BOTTOM)vita2d_draw_rectangle(0,bottom,960,544-bottom,VT_THEME_SURFACE_RAISED);
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];
 CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 language=1;host_reduced_motion=0;
 for(int theme=VT_UI_THEME_APERTURE;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);frame=0;ui_theme_scene_enter(UI_SCENE_HOME,"Audio locale");
  for(int i=0;i<36;i++){
   game_frame(theme,i*2,UI_CONTENT_BOTTOM);
   char name[96];snprintf(name,sizeof(name),"new-theme-%d-frame-%03d",theme,i);png(name);
  }
 }
 FT_Done_Face(face);FT_Done_FreeType(ft);
 puts("Production theme opening previews rendered on host; synthetic library, not Vita capture.");
}
