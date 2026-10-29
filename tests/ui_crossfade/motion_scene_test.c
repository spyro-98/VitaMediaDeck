/* Production drawing/geometry, synthetic data; these are not Vita captures. */
#define VMD_ONLINE_UI
#define main original_iptv_main
#include "../iptv/ui_test.c"
#undef main
void ui_home_tab_set(int tab){(void)tab;}
void ui_online_audio_set_home(int home){(void)home;}
static UiThemeRect motion_rect(int theme,int index,int first,int bottom) {
 if(theme)return ui_theme_browser_rect(theme,index-first,bottom,1);
 UiMediaGridLayout grid=ui_media_grid_layout(110,bottom);
 return ui_theme_page_rect(UI_SCENE_BROWSER,(UiThemeRect){18+index%3*311,
  110+(index/3-first/3)*grid.step,302,grid.card_h});
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));
 FT_Face inter,mono;CHECK(!FT_New_Face(ft,argv[1],0,&inter));
 CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));
 language=1;unsigned max_items=0,max_vertices=0,max_primitives=0;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;
  int bottom=536,count=120;
  UiThemeHomeLayout l=ui_theme_browser_layout(theme,bottom,1);
  if(!theme){UiMediaGridLayout grid=ui_media_grid_layout(110,bottom);l=(UiThemeHomeLayout){18,110,302,grid.step,6,3};}
  UiFocusMotion focus={0};host_reduced_motion=1;frame=theme*300+1;
  ui_theme_page(UI_SCENE_BROWSER,"animation-test","ux0:media",0,count,l.columns,l.rows,l.step);
  host_reduced_motion=0;
  for(int tick=0;tick<90;tick++){
   frame=theme*300+2+tick;
   int first=tick<9?0:tick<18?l.rows:tick<24?0:tick<48?l.columns:0;
   const char *identity=tick>=60&&tick<69?"ux0:media/child":"ux0:media";
   if(tick==42)count+=9; /* Background catalogue append must not replay anything. */
   UiPageView v=ui_theme_page(UI_SCENE_BROWSER,"animation-test",identity,first,count,l.columns,l.rows,l.step);
   CHECK(v.end-v.first<=l.rows+l.columns);
   vita2d_clear_screen();draw_primitives=gpu_draw_calls=gpu_vertex_count=0;
   ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
   vita2d_draw_rectangle(0,0,960,54,VT_THEME_SURFACE);
   ui_text_line(18,34,700,"VitaMediaDeck / transizioni",VT_THEME_TEXT,UI_FONT_BODY);
   ui_text_line(18,87,900,identity,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
   vita2d_set_clip_rectangle(0,l.y-4,960,bottom);vita2d_enable_clipping();
   int selected=first+(l.rows>1?1:0);
   for(int i=v.first;i<v.end;i++){
    UiThemeRect rect=motion_rect(theme,i,first,bottom);
    host_reduced_motion=1;UiThemeRect base=motion_rect(theme,i,first,bottom);host_reduced_motion=0;
    CHECK(rect.x-base.x==(int)roundf(v.dx)&&rect.y-base.y==(int)roundf(v.dy));
    for(int j=v.first;j<i;j++){
     UiThemeRect other=motion_rect(theme,j,first,bottom);
     CHECK(!(rect.x<other.x+other.w&&rect.x+rect.w>other.x&&rect.y<other.y+other.h&&rect.y+rect.h>other.y));
    }
    char title[64];snprintf(title,sizeof(title),"%s %02d",i%3?"Cartella":"Video",i+1);
    if(theme)ui_theme_browser_tile(UI_SCENE_BROWSER,rect,i-first,i==selected,
      vt_artwork_thumbnail_get("fixture-square.png"),title,"Archivio / 12 elementi",i%3!=0);
    else{
     ui_panel(rect.x,rect.y,rect.w,rect.h,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
     ui_artwork_tile(rect.x+10,rect.y+8,rect.h-74,vt_artwork_thumbnail_get("fixture-square.png"),"DIR");
     ui_text_line(rect.x+12,rect.y+rect.h-34,rect.w-24,title,VT_THEME_TEXT,UI_FONT_SMALL);
     ui_text_line(rect.x+12,rect.y+rect.h-12,rect.w-24,"Archivio / 12 elementi",VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
    }
   }
   vita2d_disable_clipping();
   ui_theme_page_finish(UI_SCENE_BROWSER,18,l.y,924,bottom);
   UiThemeRect selected_rect=motion_rect(theme,selected,first,bottom);
   ui_focus_motion_tick(&focus,selected_rect.x,selected_rect.y,selected_rect.w,selected_rect.h);
   ui_focus_glow_draw(focus.x,focus.y,focus.width,focus.height,0,l.y,bottom);
   if((unsigned)(v.end-v.first)>max_items)max_items=(unsigned)(v.end-v.first);
   if(gpu_vertex_count>max_vertices)max_vertices=gpu_vertex_count;
   CHECK(gpu_vertex_count<=(theme==VT_UI_THEME_LEGACY_VITA?7830u:2304u));
   if(draw_primitives>max_primitives)max_primitives=draw_primitives;
   if(tick%3==0){char name[80];snprintf(name,sizeof(name),"motion-%d-%03d",theme,tick);png(name);}
  }
  if(theme){
   UiThemeHomeLayout home=ui_theme_home_layout(theme,bottom);
   UiFocusMotion home_focus={0};host_reduced_motion=1;
   ui_theme_page(UI_SCENE_HOME,"home-test","library",0,120,home.columns,home.rows,home.step);
   host_reduced_motion=0;
   const char *labels[]={"Libreria","Video","Musica","Radio","Podcast","Immagini"};
   for(int tick=0;tick<30;tick++){
    frame=theme*300+100+tick;
    int first=tick<7?0:tick<16?home.rows:0;
    const char *key=tick<20?"library":"music";
    UiPageView v=ui_theme_page(UI_SCENE_HOME,"home-test",key,first,120,home.columns,home.rows,home.step);
    vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
   vita2d_draw_rectangle(0,0,960,54,VT_THEME_SURFACE);
    ui_text_line(18,34,700,"VitaMediaDeck / Home",VT_THEME_TEXT,UI_FONT_BODY);
    ui_theme_home_menu(theme,tick<20?0:2,labels,NULL,0,&home_focus);
    ui_theme_home_heading(tick<20?"Libreria":"Musica","Archivio locale",bottom);
    vita2d_set_clip_rectangle(0,home.y-4,960,bottom);vita2d_enable_clipping();
    for(int i=v.first;i<v.end;i++){
     char title[64];snprintf(title,sizeof(title),"Cartella %02d",i+1);
     ui_theme_home_row_art(i-first,i,title,"12 elementi",i==first,1,bottom,
         vt_artwork_thumbnail_get("fixture-square.png"),"DIR");
    }
    vita2d_disable_clipping();
    ui_theme_home_footer("Archivio locale",bottom);ui_theme_home_position(first,120,bottom);
    if(tick%3==0){char name[80];snprintf(name,sizeof(name),"home-motion-%d-%03d",theme,tick);png(name);}
   }
  }
  /* Animated command-deck hitboxes must follow the exact same panel transform. */
  UiSectionsSidebar menu;live_slots=download_slots=1;ui_sections_sidebar_init(&menu,UI_SECTION_LOCAL_MEDIA);
  for(int n=1;n<=4;n++){
   menu.open=1;menu.animation=n*.25f;menu.focus_cursor=3;
   for(int slot=0;slot<UI_SECTION_COUNT+2;slot++){
    menu.cursor=slot;sidebar_scroll_to_cursor(&menu);menu.scroll=menu.scroll_target;
    UiThemeRect hit=sidebar_slot_rect(&menu,slot);
    int x=(int)sidebar_offset(&menu)+hit.x+hit.w/2,y=hit.y+hit.h/2;
    if(x<0||x>=960||y<sidebar_view_top()||y>=sidebar_view_bottom())continue;
    UiSectionsSidebar test=menu;test.active=UI_SECTION_NONE;
    CHECK(ui_sections_sidebar_handle_touch(&test,UI_TOUCH_EVENT_TAP,x,y)==sidebar_section_at(&menu,slot));
   }
  }
  live_slots=0;download_slots=-1;
 }
 /* VMD's existing full-screen wave mesh has 7,830 vertices; other themes
  * retain their smaller scene budget. This is geometry, not a timing test. */
 CHECK(max_items<=10&&max_vertices<=7830&&max_primitives<900);
 FT_Done_Face(inter);FT_Done_Face(mono);FT_Done_FreeType(ft);
 printf("Continuous native drawing: %u checks, 8 themes, retained outgoing rows, uniform "
        "transforms, no overlaps, animated touch and cache append; max %u items, %u vertices, %u "
        "primitives/frame (host).\n",
        checks, max_items, max_vertices, max_primitives);
}
