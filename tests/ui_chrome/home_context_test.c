#define VMD_CHROME_HELPERS_ONLY
#include "test.c"
#include "../../src/ui/home_dashboard.c"
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);host_record_text=1;host_text_count=0;ui_home_tab_set(HOME_LIBRARY);
  vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
  ui_brand_draw_home_header();ui_brand_draw_home_search();UiFocusMotion motion={0};ui_home_tabs_draw_with_focus(1,&motion);
  if(vt_ui_theme_legacy_layout(theme)){
   CHECK(UI_HOME_TABS_Y>=0&&UI_HOME_TABS_BOTTOM<=UI_BRAND_HEADER_HEIGHT);
   CHECK(ui_brand_home_search_hit(UI_TOUCH_EVENT_TAP,94,24));
   CHECK(!ui_brand_home_search_hit(UI_TOUCH_EVENT_TAP,94,78));
   for(int i=0;i<host_text_count;i++)CHECK(host_text_rects[i].y<=UI_BRAND_HEADER_HEIGHT);
   UiDashboardLayout home=ui_dashboard_layout(theme,536);
   CHECK(home.title_y-UI_FONT_BODY>=UI_BRAND_HEADER_HEIGHT+8);
   ui_text_line(home.content.x,home.title_y,home.content.w,"Your media, together",VT_THEME_TEXT,UI_FONT_BODY);
   char name[64];snprintf(name,sizeof(name),"home-chrome-corrected-%d-%u",theme,language);png(name);
  }
  host_text_count=0;trail_text[0]=0;drawing_trail=1;
  ui_breadcrumb_draw(HEADER_NAME_X,696,language?"Video locali":"Local video","ux0:video","Anime",1);
  drawing_trail=0;
  CHECK(strstr(trail_text,"Anime")&&strstr(trail_text,language?"Video locali":"Local video"));
  host_text_count=0;
  ui_brand_draw_path(language?"Video locali":"Local video","ux0:video/Anime");
  for(int i=0;i<host_text_count;i++)CHECK(host_text_rects[i].x+host_text_rects[i].width<=960);
  
  for(int i=0;i<host_text_count;i++)CHECK(host_text_rects[i].y<=54);
 }
 FT_Done_Face(face);FT_Done_FreeType(ft);printf("Home/header context: %u checks; tabs/search in the chrome, no overlap with Home title, contextual local folder paths, eight themes and IT/EN passed.\n",checks);
}
