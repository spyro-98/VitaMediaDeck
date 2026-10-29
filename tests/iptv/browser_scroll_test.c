#define main previous_ui_main
#include "ui_test.c"
#undef main
static unsigned left_moved,right_moved;
static void observe_scroll(unsigned tick){
 if(tick==8){CHECK(picker_scroll.position>40);CHECK(catalog_top==0&&catalog_focus==0);left_moved++;}
 if(tick==28){CHECK(catalog_top>0&&catalog_focus>0);CHECK(!picker_focus);right_moved++;}
 CHECK(!input_fetches&&!plays);
}
static int original_layout,seen_switch;
static void observe_switch(unsigned tick){
 (void)tick;
 if(!saved_view&&split_view!=original_layout){
  CHECK(catalog.count==8&&catalog_focus==3&&!strcmp(current_list.url,"https://example.org/active.m3u"));seen_switch++;
 }
 CHECK(!input_fetches&&!plays);
}
static void switch_loaded(int layout){
 new_link();host_iptv_split=split_view=original_layout=layout;picker_focus=0;saved_view=0;
 snprintf(current_list.url,sizeof(current_list.url),"https://example.org/active.m3u");current_list.mode=VT_IPTV_LIST;
 catalog.count=8;catalog_focus=3;
 for(unsigned i=0;i<8;i++){
  catalog.items[i]=(VtIptvChannel){.mode=VT_IPTV_HLS};
  snprintf(catalog.items[i].title,sizeof(catalog.items[i].title),"Loaded channel %u",i);
  snprintf(catalog.items[i].url,sizeof(catalog.items[i].url),"https://example.org/loaded/%u.m3u8",i);
 }
 filter_dirty=1;refresh_playlists();filter_catalog();
 UiContextAction actions[16];int count=options_for_page(actions),index=-1;
 for(int i=0;i<count;i++)if(actions[i].action==22)index=i;
 CHECK(index>=0);memset(input_script,0,sizeof(input_script));frame=axis_count=host_touch_count=0;
 unsigned at=1;input_script[at++]=SCE_CTRL_RTRIGGER;at++;
 for(int i=0;i<index;i++){input_script[at++]=SCE_CTRL_DOWN;at++;}
 input_script[at++]=SCE_CTRL_CROSS;at+=18;input_script[at++]=SCE_CTRL_CIRCLE;at++;input_script[at++]=SCE_CTRL_CIRCLE;
 script_count=at;frame_observer=observe_switch;seen_switch=0;
 CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA&&seen_switch&&host_iptv_split!=layout);
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 host_saved_catalog=calloc(1,sizeof(*host_saved_catalog));CHECK(host_saved_catalog);host_saved_catalog->count=100;
 for(unsigned i=0;i<100;i++){
  VtIptvChannel *p=&host_saved_catalog->items[i];p->mode=VT_IPTV_LIST;
  snprintf(p->title,sizeof(p->title),"Playlist %u",i);snprintf(p->url,sizeof(p->url),"https://example.org/list/%u.m3u",i);
 }
 new_link();host_iptv_split=1;saved_view=0;catalog.count=200;
 for(unsigned i=0;i<200;i++){
  catalog.items[i].mode=VT_IPTV_HLS;snprintf(catalog.items[i].title,sizeof(catalog.items[i].title),"Channel %u",i);
  snprintf(catalog.items[i].url,sizeof(catalog.items[i].url),"https://example.org/channel/%u.m3u8",i);
 }
 ui_theme_apply(VT_UI_THEME_LEGACY_VITA);mini_top=544;frame=0;script_count=axis_count=host_touch_count=55;
 memset(input_script,0,sizeof(input_script));memset(input_axes,0,sizeof(input_axes));
 for(unsigned i=0;i<55;i++){input_axes[i].lx=input_axes[i].ly=input_axes[i].rx=input_axes[i].ry=128;}
 for(unsigned i=12;i<31;i++)input_axes[i].ly=255;
 host_touch_script[1].flags=UI_TOUCH_EVENT_DOWN;host_touch_script[1].event=(UiTouchEvent){.x=60,.y=400,.down_x=60,.down_y=400};
 host_touch_script[2].flags=UI_TOUCH_EVENT_MOVE;host_touch_script[2].event=(UiTouchEvent){.x=60,.y=340,.down_x=60,.down_y=400};
 host_touch_script[3].flags=UI_TOUCH_EVENT_UP;host_touch_script[3].event=(UiTouchEvent){.x=60,.y=280,.down_x=60,.down_y=400};
 input_script[10]=SCE_CTRL_RIGHT;input_script[50]=SCE_CTRL_CIRCLE;input_script[52]=SCE_CTRL_CIRCLE;
 frame_observer=observe_scroll;CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);CHECK(left_moved==1&&right_moved==1);
 switch_loaded(0);switch_loaded(1);
 for(int layout=0;layout<2;layout++){
  new_link();host_iptv_split=layout;frame=axis_count=host_touch_count=0;frame_observer=NULL;
  input_fetches=plays=0;input_list_fixture="<html>Not an M3U playlist</html>";
  const unsigned failed[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
  memcpy(input_script,failed,sizeof(failed));script_count=sizeof(failed)/sizeof(failed[0]);
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA&&input_fetches==1&&!plays);
  CHECK(saved_view&&!saved_channels&&catalog.count==100);
 }

 frame_observer=NULL;free(host_saved_catalog);host_saved_catalog=NULL;
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("IPTV independent pane scrolling: %u checks; left touch drag/inertia, right analog pixel scrolling, correct focus, loaded-catalogue/selection retention during both R1 switches failed-playlist return to the browser and no implicit fetch/play passed. Host, not Vita timing.\n",checks);
}
