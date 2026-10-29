/* Production rendering on the host; synthetic library, not console evidence. */
#define main original_iptv_main
#define VMD_ONLINE_UI
#include "../iptv/ui_test.c"
#undef main
#include "ui/home_services.h"
#include "ui/frame_pacing.h"
void ui_home_tab_set(int tab){(void)tab;}
void ui_online_audio_set_home(int favorites){(void)favorites;}
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
int main(int argc,char **argv) {
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 for(int filter=-1;filter<6;filter++) {
  int n=home_provider_count(filter);CHECK(n==(filter==0?2:filter==2||filter==3?1:0));
  CHECK(!home_provider_item(filter,-1));CHECK(!home_provider_item(filter,n));
  for(int i=0;i<n;i++) {
   const VtLocalMediaItem *item=home_provider_item(filter,i);
   CHECK(item&&item->is_directory&&item->source!=VT_LOCAL_MEDIA_SOURCE_FILE);
   CHECK(home_provider_section(item)==(filter==3||i==1?UI_SECTION_IMMICH:UI_SECTION_NAVIDROME));
   CHECK(!item->size&&!item->artwork_path[0]);
  }
 }
 for(int local=0;local<100;local++)for(int filter=0;filter<5;filter++) {
  int total=local+home_provider_count(filter),seen=0;
  for(int position=0;position<total;position++){
   int provider=home_provider_position(filter,position,local),file=home_local_position(filter,position,local);
   CHECK((provider>=0)!=(file>=0));
   if(provider>=0){seen++;CHECK(file<0);if(filter==0)CHECK(position>=local);else CHECK(position==0);}
   else CHECK(file>=0&&file<local);
  }
  CHECK(seen==home_provider_count(filter));CHECK(home_local_position(filter,total,local)<0);
 }
 VtLocalMediaItem disk={.source=VT_LOCAL_MEDIA_SOURCE_FILE};
 CHECK(home_provider_section(&disk)==UI_SECTION_NONE);CHECK(home_provider_section(NULL)==UI_SECTION_NONE);
 /* Every row maps to the same section for controller, touch and active page,
  * with independent player/download lifetimes and non-focusable group labels. */
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)
 for(live_slots=0;live_slots<2;live_slots++)for(download_slots=0;download_slots<2;download_slots++) {
  ui_theme_apply(theme);int live=live_slots+download_slots;
  for(int section=0;section<UI_SECTION_COUNT;section++) {
   UiSectionsSidebar sidebar;ui_sections_sidebar_init(&sidebar,section);
   CHECK(sidebar_section_at(&sidebar,sidebar.cursor)==section);
   unsigned press=SCE_CTRL_LTRIGGER;CHECK(ui_sections_sidebar_handle_buttons(&sidebar,&press,0,128)==UI_SECTION_NONE);
   CHECK(sidebar.open&&!press&&sidebar_section_at(&sidebar,sidebar.cursor)==section);
  }
  UiSectionsSidebar sidebar;ui_sections_sidebar_init(&sidebar,UI_SECTION_LOCAL_MEDIA);
  int prev=0;
  for(int slot=0;slot<UI_SECTION_COUNT+live;slot++) {
   sidebar.cursor=slot;sidebar_scroll_to_cursor(&sidebar);sidebar.scroll=sidebar.scroll_target;
   UiThemeRect r=sidebar_slot_rect(&sidebar,slot);int y=sidebar_content_rect(&sidebar,slot).y;
   CHECK(r.x>=18&&r.y>=sidebar_view_top()+17&&r.x+r.w<=942&&r.y+r.h<=sidebar_view_bottom()-17);
   CHECK(r.h>=42);
   if (vt_ui_theme_legacy_layout(theme))
       CHECK(y > prev);
   prev = y;
   CHECK(sidebar_section_at(&sidebar,live+2)==UI_SECTION_JELLYFIN);
   CHECK(sidebar_section_at(&sidebar,live+3)==UI_SECTION_NAVIDROME);
   CHECK(sidebar_section_at(&sidebar,live+4)==UI_SECTION_IMMICH);
   for(int other=0;other<slot;other++) {
    UiThemeRect b=sidebar_slot_rect(&sidebar,other);
    CHECK(r.x+r.w<=b.x || b.x+b.w<=r.x || r.y+r.h<=b.y || b.y+b.h<=r.y);
   }
   int section=sidebar_section_at(&sidebar,slot);
   sidebar.open=1;sidebar.animation=1;sidebar.active=-1;
   CHECK(ui_sections_sidebar_handle_touch(&sidebar,UI_TOUCH_EVENT_TAP,r.x+r.w/2,r.y+r.h/2)==section);
   sidebar.open=1;sidebar.cursor=slot;sidebar.active=-1;unsigned press=SCE_CTRL_CROSS;
   CHECK(ui_sections_sidebar_handle_buttons(&sidebar,&press,0,128)==section);CHECK(!sidebar.open&&!press);
  }
  sidebar.active=UI_SECTION_LOCAL_MEDIA;sidebar.cursor=0;sidebar.open=1;sidebar.animation=1;sidebar.focus_cursor=0;
  for (int slot = 1; sidebar_vertical() && slot < UI_SECTION_COUNT + live; slot++) {
      unsigned press = SCE_CTRL_DOWN;
      ui_sections_sidebar_handle_buttons(&sidebar, &press, 0, 128);
      CHECK(sidebar.cursor == slot);
  }
  for (int slot = UI_SECTION_COUNT + live - 2; sidebar_vertical() && slot >= 0; slot--) {
      unsigned press = SCE_CTRL_UP;
      ui_sections_sidebar_handle_buttons(&sidebar, &press, 0, 128);
      CHECK(sidebar.cursor == slot);
  }
  if (vt_ui_theme_legacy_layout(theme)) {
      CHECK(sidebar_section_at(&sidebar, live + 2) == UI_SECTION_JELLYFIN);
      CHECK(sidebar_section_at(&sidebar, live + 3) == UI_SECTION_NAVIDROME);
      CHECK(sidebar_section_at(&sidebar, live + 4) == UI_SECTION_IMMICH);
      int starts[] = {0, live, live + SIDEBAR_CONTENT_COUNT};
      for (int i = live ? 0 : 1; i < 3; i++) {
          sidebar.cursor = starts[i];
          sidebar_scroll_to_cursor(&sidebar);
          sidebar.scroll = sidebar.scroll_target;
          int heading = sidebar_slot_rect(&sidebar, starts[i]).y - 14;
          sidebar.open = 1;
          CHECK(ui_sections_sidebar_handle_touch(&sidebar, UI_TOUCH_EVENT_TAP, 100, heading) ==
                UI_SECTION_NONE);
          CHECK(sidebar.open);
      }
  }
  if (!vt_ui_theme_legacy_layout(theme)) {
      UiSectionsSidebar audio;
      ui_sections_sidebar_init(&audio, UI_SECTION_LOCAL_MEDIA);
      audio.open = 1;
      audio.animation = 1;
      audio.audio_layout_available = 1;
      unsigned press = SCE_CTRL_RIGHT;
      ui_sections_sidebar_handle_buttons(&audio, &press, 0, 128);
      CHECK(audio.audio_layout_page && press == 0);
      for (int i = 0; i < 3; i++) {
          UiThemeRect r = sidebar_layout_rect(i);
          CHECK(ui_sections_sidebar_handle_touch(&audio, UI_TOUCH_EVENT_TAP, r.x + r.w / 2,
                                                 r.y + r.h / 2) ==
                UI_SECTION_AUDIO_LAYOUT_BASE + i);
          CHECK(audio.audio_layout_cursor == i);
          press = SCE_CTRL_CROSS;
          CHECK(ui_sections_sidebar_handle_buttons(&audio, &press, 0, 128) ==
                UI_SECTION_AUDIO_LAYOUT_BASE + i);
      }
      ui_sections_sidebar_tick(&audio);
      vita2d_clear_screen();
      ui_sections_sidebar_draw(&audio);
      if (language && live_slots == 1 && download_slots == 1) {
          char name[80];
          snprintf(name, sizeof(name), "theme-player-layout-%d", theme);
          png(name);
      }
      CHECK(ui_sections_sidebar_handle_touch(&audio, UI_TOUCH_EVENT_TAP, 100, 75) ==
                UI_SECTION_NONE &&
            !audio.audio_layout_page);
  }
  sidebar.cursor=0;sidebar.focus_cursor=0;sidebar.scroll=sidebar.scroll_target=sidebar.scroll_velocity=0;
  vita2d_clear_screen();ui_sections_sidebar_draw(&sidebar);
  if(language&&live_slots==1&&download_slots==1){char name[80];snprintf(name,sizeof(name),"theme-l1-%d",theme);png(name);}
 }
 ui_theme_apply(VT_UI_THEME_LEGACY);live_slots=1;download_slots=1;
 UiSectionsSidebar changing;ui_sections_sidebar_init(&changing,UI_SECTION_IMMICH);
 changing.open=1;live_slots=0;download_slots=0;ui_sections_sidebar_tick(&changing);
 CHECK(sidebar_section_at(&changing,changing.cursor)==UI_SECTION_IMMICH);
 live_slots=1;ui_sections_sidebar_tick(&changing);
 CHECK(sidebar_section_at(&changing,changing.cursor)==UI_SECTION_IMMICH);
 changing.open=0;unsigned reopen=SCE_CTRL_LTRIGGER;
 ui_sections_sidebar_handle_buttons(&changing,&reopen,0,128);
 CHECK(changing.open&&sidebar_section_at(&changing,changing.cursor)==UI_SECTION_IMMICH);
 live_slots=0;download_slots=-1;
 unsigned max_primitives=0;
 /* This composition is only for the six alternative layouts. VMD's actual
  * Legacy-style composition is rendered in home_test.c, not by these helpers. */
 for(int theme=1;theme<VT_UI_THEME_LEGACY_VITA;theme++)for(language=0;language<2;language++)for(int mini=0;mini<2;mini++) {
  ui_theme_apply(theme);int bottom=mini?472:UI_CONTENT_BOTTOM;
  host_reduced_motion=1;game_frame(theme,0,bottom);memcpy(first_pixels,pixels,sizeof(pixels));
  game_frame(theme,120,bottom);CHECK(!memcmp(first_pixels,pixels,sizeof(pixels)));
  host_reduced_motion=0;game_frame(theme,0,bottom);memcpy(first_pixels,pixels,sizeof(pixels));
  game_frame(theme,120,bottom); /* Clean menu canvases may remain still while idle. */
  if(draw_primitives>max_primitives)max_primitives=draw_primitives;
  CHECK(draw_primitives<1400);
  char name[80];snprintf(name,sizeof(name),"game-home-%d-%s%s",theme,language?"it":"en",mini?"-mini":"");png(name);
 }
 CHECK(ui_frame_remaining_us(0,9000)==7667);CHECK(!ui_frame_remaining_us(0,16667));
 CHECK(!ui_frame_remaining_us(0,40000));CHECK(!ui_frame_remaining_us(200,100));
 ui_theme_apply(VT_UI_THEME_GHOST_MESH);host_reduced_motion=0;
 for(int i=0;i<6;i++){
  UiThemeRect a=ui_theme_home_tab_rect(VT_UI_THEME_GHOST_MESH,i,1);
  UiThemeRect opposite=ui_theme_home_tab_rect(VT_UI_THEME_GHOST_MESH,(i+3)%6,1);
  float x=a.x+a.w*.5f,y=a.y+a.h*.5f;
  CHECK(fabsf(hypotf(x-690,y-272)-164)<.6f);
  CHECK(a.x+opposite.x+170==1380&&a.y+opposite.y+40==544);
 }
 printf("Game UI/providers/Legacy L1: %u checks passed; max %u geometry primitives/frame (host, not Vita timing).\n",checks,max_primitives);
 FT_Done_Face(face);FT_Done_FreeType(ft);return 0;
}
