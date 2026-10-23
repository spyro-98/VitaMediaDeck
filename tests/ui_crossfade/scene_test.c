/* Actual scene, browser tile, audio and L1 drawing. Fixtures are synthetic.
 * Runtime/decoding/performance on Vita still needs physical-console evidence. */
#define VMD_AUDIO_TEST_LIBRARY
#include "audio_test.c"
#undef VMD_AUDIO_TEST_LIBRARY
static int overlaps(UiThemeRect a,UiThemeRect b) {
 return a.x<b.x+b.w && a.x+a.w>b.x && a.y<b.y+b.h && a.y+a.h>b.y;
}
static void label_file(char *out,int theme,int role,int mini) {
 snprintf(out,96,"scene-%d-%s%s",theme,role==0?"folders":role==1?"hud":"player",mini?"-mini":"");
}
int main(int argc,char **argv) {
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 language=1;unsigned max_vertices=0;host_reduced_motion=1;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++) {
  ui_theme_apply(theme);frame=0;
  for(int scene=0;scene<UI_SCENE_COUNT;scene++) {
   gpu_draw_calls=gpu_vertex_count=0;ui_theme_scene_model(scene,480,272,220,1,1);
   CHECK(theme?gpu_vertex_count>0:!gpu_vertex_count);CHECK(gpu_vertex_count<=1152);
   vita2d_clear_screen();ui_theme_scene_model(scene,480,272,220,1,1);memcpy(first_pixels,pixels,sizeof(pixels));
   frame+=10;vita2d_clear_screen();ui_theme_scene_model(scene,480,272,220,1,1);CHECK(!memcmp(first_pixels,pixels,sizeof(pixels)));
  }
  if(!theme)continue;
  for(int mini=0;mini<2;mini++)for(int grid=0;grid<2;grid++) {
   int bottom=mini?436:536;UiThemeHomeLayout l=ui_theme_browser_layout(theme,bottom,grid);
   CHECK(l.columns>0&&l.rows>=l.columns&&l.rows%l.columns==0);
   ui_theme_scene_begin(UI_SCENE_BROWSER,"ux0:media/Music");
   vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
   gpu_draw_calls=gpu_vertex_count=0;
   for(int i=0;i<l.rows;i++) {
    UiThemeRect rr=ui_theme_browser_rect(theme,i,bottom,grid);
    CHECK(rr.x>=18&&rr.x+rr.w<=942&&rr.y>=104&&rr.y+rr.h<=bottom);
    for(int j=0;j<i;j++)CHECK(!overlaps(rr,ui_theme_browser_rect(theme,j,bottom,grid)));
    ui_theme_browser_tile(UI_SCENE_BROWSER,rr,i,i==1,vt_artwork_thumbnail_get("fixture.png"),
       i%2?"Night transit / memoria":"Ghost in the Shell",i%2?"FLAC / 48 kHz":"Cartella / 12 elementi",i%2==0);
   }
   if(gpu_vertex_count>max_vertices)max_vertices=gpu_vertex_count;
   ui_text_line(24,86,900,"Archivio / Music",VT_THEME_TEXT,UI_FONT_BODY);
   if(mini)vita2d_draw_rectangle(0,bottom,960,544-bottom,VT_THEME_SURFACE_RAISED);
   if(grid){char name[96];label_file(name,theme,0,mini);png(name);}
  }
  /* Every settled L1 target is distinct, with both transient live actions. */
  live_slots=download_slots=1;UiSectionsSidebar menu;ui_sections_sidebar_init(&menu,UI_SECTION_LOCAL_MEDIA);
  menu.open=1;menu.animation=1;
  for(int i=0;i<UI_SECTION_COUNT+2;i++)for(int j=0;j<i;j++)CHECK(!overlaps(sidebar_slot_rect(&menu,i),sidebar_slot_rect(&menu,j)));
  live_slots=0;download_slots=-1;
  /* Moving geometry is finite, settles on the same target, and remains inside
   * the safe viewport even after rapid folder reversals and clock resets. */
  host_reduced_motion=0;
  for(int pass=0;pass<3;pass++) {
   frame=pass*40;ui_theme_scene_begin(UI_SCENE_BROWSER,pass%2?"parent":"child");
   UiThemeRect first=ui_theme_browser_rect(theme,0,536,1);
   frame+=1;UiThemeRect second=ui_theme_browser_rect(theme,0,536,1);
   CHECK(first.x==second.x&&first.y==second.y);
   frame+=30;UiThemeRect settled=ui_theme_browser_rect(theme,0,536,1);
   host_reduced_motion=1;UiThemeRect exact=ui_theme_browser_rect(theme,0,536,1);CHECK(!memcmp(&settled,&exact,sizeof(exact)));host_reduced_motion=0;
  }
  for(int grid=0;grid<2;grid++) {
   frame=0;ui_theme_scene_begin(UI_SCENE_BROWSER,grid?"animated-grid":"animated-list");
   for(int tick=0;tick<25;tick++) {
    frame=tick;UiThemeHomeLayout l=ui_theme_browser_layout(theme,536,grid);
    for(int i=0;i<l.rows;i++)for(int j=0;j<i;j++)CHECK(!overlaps(ui_theme_browser_rect(theme,i,536,grid),ui_theme_browser_rect(theme,j,536,grid)));
   }
  }
  frame=0;ui_theme_scene_begin(UI_SCENE_MENU,"animated-menu");
  live_slots=download_slots=1;ui_sections_sidebar_init(&menu,UI_SECTION_LOCAL_MEDIA);
  for(int tick=0;tick<25;tick++){frame=tick;for(int i=0;i<UI_SECTION_COUNT+2;i++)for(int j=0;j<i;j++)CHECK(!overlaps(sidebar_slot_rect(&menu,i),sidebar_slot_rect(&menu,j)));}
  live_slots=0;download_slots=-1;
  /* Paused audio freezes geometry; resuming changes the actual projected mesh. */
  frame=0;ui_theme_scene_begin(UI_SCENE_AUDIO,"fixture");vita2d_clear_screen();ui_theme_scene_model(UI_SCENE_AUDIO,480,250,220,1,0);
  memcpy(first_pixels,pixels,sizeof(pixels));frame=10;vita2d_clear_screen();ui_theme_scene_model(UI_SCENE_AUDIO,480,250,220,1,0);CHECK(!memcmp(first_pixels,pixels,sizeof(pixels)));
  for(int i=0;i<24;i++){frame+=2;vita2d_clear_screen();ui_theme_scene_model(UI_SCENE_AUDIO,480,250,220,1,1);}CHECK(memcmp(first_pixels,pixels,sizeof(pixels)));
  host_reduced_motion=1;
  host_reduced_motion=0;frame=0;ui_theme_scene_enter(UI_SCENE_MENU,"reopen");UiThemeRect reopened=ui_theme_scene_reveal(UI_SCENE_MENU,(UiThemeRect){104,234,250,58},0);
  frame=30;UiThemeRect end=ui_theme_scene_reveal(UI_SCENE_MENU,(UiThemeRect){104,234,250,58},0);CHECK(reopened.x==end.x&&reopened.y==end.y);
  ui_theme_scene_enter(UI_SCENE_MENU,"reopen");UiThemeRect again=ui_theme_scene_reveal(UI_SCENE_MENU,(UiThemeRect){104,234,250,58},0);CHECK(again.x==reopened.x&&again.y==reopened.y);
  host_reduced_motion=1;
  /* Bounds/hit regions for timeline and each video transport. */
  UiThemeVideoLayout v=ui_theme_video_layout();CHECK(v.back_x+68<v.play_x&&v.play_x+68<v.next_x);
  CHECK(v.seek_x>=18&&v.seek_x+v.seek_w<=942&&v.seek_y>v.y+34);
  vita2d_clear_screen();vita2d_draw_rectangle(0,0,960,544,RGBA8(25,36,52,255));
  ui_theme_video_scrim(1);ui_theme_scene_model(UI_SCENE_VIDEO,230,456,90,.85f,0);
  ui_theme_scene_transport(v.play_x-24,v.y-22,48,44,1,1);
  vita2d_draw_rectangle(v.seek_x,v.seek_y,v.seek_w,5,VT_THEME_COLD);
  char name[96];label_file(name,theme,1,0);png(name);
  /* Production audio player, each default composition with cover and no cover. */
  for(has_art=0;has_art<2;has_art++) {
   audio=(VtBackgroundPlaybackSnapshot){.state=VT_BACKGROUND_PAUSED,.duration_ms=342000,.position_ms=138000,.audio_sample_rate=48000,.audio_bitrate_kbps=256,.audio_channels=2};
   strcpy(audio.title,"Night transit / Ghost in the Shell");strcpy(audio.channel,"Studio North");
   layout_preference=0;frame=0;capture_frame=0;script_count=2;input_script[0]=0;input_script[1]=SCE_CTRL_CIRCLE;
   CHECK(ui_music_player_run("fixture","Night Transit","FLAC",1411)==UI_MUSIC_PLAYER_STOP);
   CHECK(!pause_calls&&!seek_calls);memcpy(pixels,first_pixels,sizeof(pixels));snprintf(name,sizeof(name),"scene-%d-audio-%s",theme,has_art?"cover":"no-cover");png(name);
  }
 }
 for(int theme=1;theme<VT_UI_THEME_COUNT;theme++) {
  ui_theme_apply(theme);host_reduced_motion=1;has_art=1;music_preference=VT_MUSIC_THEME_NONE;
  frame=0;capture_frame=0;script_count=2;input_script[0]=0;input_script[1]=SCE_CTRL_CIRCLE;
  CHECK(ui_music_player_run("fixture","Night Transit","FLAC",1411)==UI_MUSIC_PLAYER_STOP);
  unsigned char *plain=malloc(sizeof(pixels));CHECK(plain);memcpy(plain,first_pixels,sizeof(pixels));
  music_preference=VT_MUSIC_THEME_WARM;frame=0;
  CHECK(ui_music_player_run("fixture","Night Transit","FLAC",1411)==UI_MUSIC_PLAYER_STOP);
  CHECK(memcmp(plain,first_pixels,sizeof(pixels)));CHECK(!pause_calls&&!seek_calls);free(plain);
  music_preference=VT_MUSIC_THEME_NONE;
 }
 if(getenv("VMD_SCENE_ANIMATION"))for(int theme=1;theme<VT_UI_THEME_COUNT;theme++) {
  ui_theme_apply(theme);host_reduced_motion=0;frame=0;
  ui_theme_scene_enter(UI_SCENE_BROWSER,"animated-folder");
  for(int tick=0;tick<24;tick++) {
   frame=tick*2;vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
   ui_text_line(24,86,900,"Archivio / Music",VT_THEME_TEXT,UI_FONT_BODY);
   UiThemeHomeLayout l=ui_theme_browser_layout(theme,536,1);
   for(int i=0;i<l.rows;i++)ui_theme_browser_tile(UI_SCENE_BROWSER,ui_theme_browser_rect(theme,i,536,1),i,i==1,vt_artwork_thumbnail_get("fixture.png"),i%2?"Night transit":"Ghost in the Shell","Cartella / 12 elementi",1);
   char name[96];snprintf(name,sizeof(name),"motion-%d-%03d",theme,tick);png(name);
  }
 }
 CHECK(max_vertices<=300);printf("Native themed scenes: %u checks; browser geometry <= %u vertices/frame. Host raster, not Vita FPS.\n",checks,max_vertices);
 FT_Done_Face(face);FT_Done_FreeType(ft);return 0;
}
