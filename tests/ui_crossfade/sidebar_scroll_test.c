/* Exercise the production command deck and host rasterizer. */
#define main original_iptv_main
#define VMD_ONLINE_UI
#include "../iptv/ui_test.c"
#undef main
void ui_home_tab_set(int tab){(void)tab;}
void ui_online_audio_set_home(int favorites){(void)favorites;}
static unsigned scroll_checks;
#define SCROLL_CHECK(x) do{scroll_checks++;CHECK(x);}while(0)
static void advance(UiSectionsSidebar *s,int frames) {
 for(int i=0;i<frames;i++){frame++;ui_sections_sidebar_tick(s);SCROLL_CHECK(isfinite(s->scroll)&&isfinite(s->scroll_velocity));SCROLL_CHECK(s->scroll>=0&&s->scroll<=sidebar_scroll_max(s));}
}
static void settled(UiSectionsSidebar *s) {
 advance(s,90);SCROLL_CHECK(fabsf(s->scroll-s->scroll_target)<.02f);
 UiThemeRect r=sidebar_slot_rect(s,s->cursor);
 SCROLL_CHECK(r.y>=sidebar_view_top()+17&&r.y+r.h<=sidebar_view_bottom()-17);
}
static void button(UiSectionsSidebar *s,unsigned key) {
 unsigned pressed=key;SCROLL_CHECK(ui_sections_sidebar_handle_buttons(s,&pressed,0,128)==UI_SECTION_NONE);SCROLL_CHECK(!pressed);
}
int main(int argc,char **argv) {
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 FT_Face inter=face,mono;CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));
 unsigned max_draws=0;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)
 for(live_slots=0;live_slots<2;live_slots++)for(download_slots=0;download_slots<2;download_slots++)for(int reduced=0;reduced<2;reduced++) {
  frame=1;host_reduced_motion=reduced;ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;
  int count=UI_SECTION_COUNT+live_slots+download_slots;
  UiSectionsSidebar menu;ui_sections_sidebar_init(&menu,UI_SECTION_LOCAL_MEDIA);button(&menu,SCE_CTRL_LTRIGGER);advance(&menu,60);
  SCROLL_CHECK((sidebar_scroll_max(&menu) > 0) == sidebar_vertical());
  /* Spacious targets remain distinct in the entire canvas, not just the viewport. */
  for(int i=0;i<count;i++) {
   UiThemeRect a=sidebar_content_rect(&menu,i);SCROLL_CHECK(a.h>=42&&a.x>=18&&a.x+a.w<=942);
   for(int j=0;j<i;j++){UiThemeRect b=sidebar_content_rect(&menu,j);SCROLL_CHECK(a.x+a.w<=b.x||b.x+b.w<=a.x||a.y+a.h<=b.y||b.y+b.h<=a.y);}
  }
  if (!sidebar_vertical()) {
      const unsigned dirs[] = {SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT};
      int reached[UI_SECTION_ACTION_COUNT] = {0};
      reached[0] = 1;
      for (int iteration = 0; iteration < count; iteration++)
          for (int at = 0; at < count; at++)
              if (reached[at]) {
                  UiSectionsSidebar from = menu;
                  from.cursor = at;
                  for (int d = 0; d < 4; d++)
                      reached[sidebar_neighbour(&from, dirs[d])] = 1;
              }
      for (int i = 0; i < count; i++) {
          SCROLL_CHECK(reached[i]);
          menu.cursor = i;
          sidebar_scroll_to_cursor(&menu);
          settled(&menu);
          UiThemeRect r = sidebar_slot_rect(&menu, i);
          SCROLL_CHECK(menu.scroll == 0 && menu.scroll_target == 0);
          UiSectionsSidebar copy = menu;
          copy.active = UI_SECTION_NONE;
          copy.touch_active = 0;
          copy.audio_layout_available = 0;
          SCROLL_CHECK(ui_sections_sidebar_handle_touch(&copy, UI_TOUCH_EVENT_TAP, r.x + r.w / 2,
                                                        r.y + r.h / 2) ==
                       sidebar_section_at(&menu, i));
          for (int d = 0; d < 4; d++) {
              copy = menu;
              int expected = sidebar_neighbour(&copy, dirs[d]);
              button(&copy, dirs[d]);
              SCROLL_CHECK(copy.cursor == expected && copy.scroll == 0);
          }
      }
      ui_sections_sidebar_handle_touch(&menu, UI_TOUCH_EVENT_DOWN, 300, 260);
      ui_sections_sidebar_handle_touch(&menu, UI_TOUCH_EVENT_MOVE, 300, 170);
      ui_sections_sidebar_handle_touch(&menu, UI_TOUCH_EVENT_UP, 300, 170);
      SCROLL_CHECK(!menu.touch_active && menu.scroll == 0 && menu.scroll_target == 0);
      if (language && live_slots && download_slots && !reduced) {
          menu.cursor = sidebar_active_slot(&menu);
          settled(&menu);
          menu.animation = 1;
          draw_primitives = 0;
          vita2d_clear_screen();
          ui_sections_sidebar_draw(&menu);
          if (draw_primitives > max_draws)
              max_draws = draw_primitives;
          SCROLL_CHECK(draw_primitives < 1200 && !clipped);
          char name[80];
          snprintf(name, sizeof(name), "l1-fixed-%d", theme);
          png(name);
      }
      continue;
  }
  /* Every item is reachable with immediate selection and a settled visible border. */
  menu.cursor=0;sidebar_scroll_to_cursor(&menu);settled(&menu);
  for(int i=1;i<count;i++){button(&menu,SCE_CTRL_DOWN);SCROLL_CHECK(menu.cursor==i);settled(&menu);}
  SCROLL_CHECK(menu.scroll>0);
  for(int i=count-2;i>=0;i--){button(&menu,SCE_CTRL_UP);SCROLL_CHECK(menu.cursor==i);settled(&menu);}
  /* Retarget quickly without replaying item entrances or overshooting the canvas. */
  for(int i=0;i<count-1;i++){button(&menu,SCE_CTRL_DOWN);advance(&menu,1);}
  menu.scroll=menu.scroll_target=menu.scroll_velocity=0;sidebar_scroll_to_cursor(&menu);float target=menu.scroll_target;advance(&menu,1);
  if(reduced)SCROLL_CHECK(menu.scroll==target);else if(target>1)SCROLL_CHECK(menu.scroll>0&&menu.scroll<target);
  for(int i=0;i<count-1;i++){button(&menu,SCE_CTRL_UP);advance(&menu,1);}settled(&menu);
  /* Dragging follows the finger, clamps at both ends and cannot become a tap. */
  menu.cursor=0;menu.focus_cursor=0;menu.scroll=menu.scroll_target=menu.scroll_velocity=0;
  int touch_x=theme?300:180;
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_DOWN,touch_x,420)==UI_SECTION_NONE);
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_MOVE,touch_x,180)==UI_SECTION_NONE);
  SCROLL_CHECK(menu.scroll>0&&menu.scroll==menu.scroll_target);
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_UP|UI_TOUCH_EVENT_TAP,touch_x,180)==UI_SECTION_NONE);
  SCROLL_CHECK(menu.open&&!menu.touch_active);float stopped=menu.scroll;advance(&menu,90);SCROLL_CHECK(fabsf(menu.scroll-stopped)<.01f);
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_DOWN,touch_x,420)==UI_SECTION_NONE);
  ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_MOVE,touch_x,-2000);SCROLL_CHECK(menu.scroll==sidebar_scroll_max(&menu));
  ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_UP,touch_x,-2000);
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_DOWN,touch_x,180)==UI_SECTION_NONE);
  ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_MOVE,touch_x,2000);SCROLL_CHECK(menu.scroll==0);
  ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_UP,touch_x,2000);
  /* Footer arrows scroll the viewport and preserve the modal; header/footer
   * taps never activate an offscreen item beneath those fixed areas. */
  int before_arrow=menu.cursor,up_x=(theme?806:318)+18;
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_TAP,up_x,510)==UI_SECTION_NONE);
  SCROLL_CHECK(menu.cursor==before_arrow&&menu.scroll_target==0);
  int arrow_x=(theme?856:366)+18;
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_TAP,arrow_x,510)==UI_SECTION_NONE&&menu.open);
  SCROLL_CHECK(menu.scroll_target>0);advance(&menu,90);
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_TAP,touch_x,95)==UI_SECTION_NONE&&menu.open);
  SCROLL_CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_TAP,touch_x,530)==UI_SECTION_NONE&&menu.open);
  /* Hit testing follows the moving camera, including partially opened panels. */
  for(int progress=1;progress<=4;progress++) {
   menu.animation=progress*.25f;
   for(int i=0;i<count;i++) {
    UiThemeRect r=sidebar_slot_rect(&menu,i);int x=(int)sidebar_offset(&menu)+r.x+r.w/2,y=r.y+r.h/2;
    if(x<0||x>=960||y<sidebar_view_top()||y>=sidebar_view_bottom())continue;
    UiSectionsSidebar copy=menu;copy.active=UI_SECTION_NONE;copy.touch_active=0;
    SCROLL_CHECK(ui_sections_sidebar_handle_touch(&copy,UI_TOUCH_EVENT_TAP,x,y)==sidebar_section_at(&menu,i));
   }
  }
  menu.animation=1;menu.open=1;
  if(language&&live_slots&&download_slots&&!reduced) {
   int picks[]={0,live_slots+download_slots+3,count-1};
   for(int n=0;n<3;n++) {
    menu.cursor=picks[n];menu.focus_cursor=(float)picks[n];sidebar_scroll_to_cursor(&menu);settled(&menu);
    draw_primitives=0;vita2d_clear_screen();ui_sections_sidebar_draw(&menu);if(draw_primitives>max_draws)max_draws=draw_primitives;
    SCROLL_CHECK(draw_primitives<1200);SCROLL_CHECK(!clipped);
    char name[80];snprintf(name,sizeof(name),"l1-scroll-%d-%d",theme,n);png(name);
   }
  }
 }
 /* Live task removal keeps the focused provider, even if it is not active. */
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++) {
  ui_theme_apply(theme);live_slots=download_slots=1;UiSectionsSidebar menu;ui_sections_sidebar_init(&menu,UI_SECTION_LOCAL_MEDIA);
  menu.open=1;menu.cursor=5;sidebar_scroll_to_cursor(&menu);settled(&menu);int selected=sidebar_section_at(&menu,menu.cursor);
  live_slots=download_slots=0;advance(&menu,1);SCROLL_CHECK(sidebar_section_at(&menu,menu.cursor)==selected);settled(&menu);
  live_slots=download_slots=1;advance(&menu,1);SCROLL_CHECK(sidebar_section_at(&menu,menu.cursor)==selected);settled(&menu);
  /* The audio presentation page remains independent of the section camera. */
  menu.audio_layout_available=1;float previous=menu.scroll_target;button(&menu,SCE_CTRL_RIGHT);SCROLL_CHECK(menu.audio_layout_page);
  ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_DOWN,200,400);ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_MOVE,200,160);ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_UP,200,160);
  SCROLL_CHECK(menu.scroll_target==previous);button(&menu,SCE_CTRL_LEFT);SCROLL_CHECK(!menu.audio_layout_page);settled(&menu);
 }
 /* Held d-pad repeats still reach the end and keep the camera following. */
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++) {
  ui_theme_apply(theme);host_reduced_motion=0;live_slots=download_slots=1;
  if (!sidebar_vertical())
      continue;
  UiSectionsSidebar held;ui_sections_sidebar_init(&held,UI_SECTION_LOCAL_MEDIA);held.open=1;held.cursor=0;
  for(int n=0;n<240;n++) {
   unsigned pressed=0;ui_sections_sidebar_handle_buttons(&held,&pressed,SCE_CTRL_DOWN,128);
   SCROLL_CHECK(!pressed);advance(&held,1);
   UiThemeRect raw=sidebar_content_rect(&held,held.cursor),shown=sidebar_slot_rect(&held,held.cursor);
   SCROLL_CHECK(shown.y-raw.y==-(int)lroundf(held.scroll));
  }
  SCROLL_CHECK(held.cursor==UI_SECTION_COUNT+1);settled(&held);
 }
 /* Clock rollback and a theme change cannot leave a stale/unbounded camera. */
 UiSectionsSidebar menu;ui_sections_sidebar_init(&menu,UI_SECTION_INFO);menu.open=1;frame=0;advance(&menu,1);
 ui_theme_apply(VT_UI_THEME_GHOST_MESH);advance(&menu,1);SCROLL_CHECK(menu.scroll_theme==VT_UI_THEME_GHOST_MESH);settled(&menu);
 FT_Done_Face(mono);FT_Done_Face(inter);FT_Done_FreeType(ft);
 printf("L1 lists/fixed decks: %u checks; seven themes, IT/EN, live tasks, spatial navigation and "
        "zero-scroll grids/wheel, rapid reversal, clipped touch, drag release, arrows, reduced "
        "motion and audio page isolation; max %u primitives/frame (host).\n",
        scroll_checks, max_draws);
 return 0;
}
