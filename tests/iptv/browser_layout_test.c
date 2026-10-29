/* Production IPTV event loop: distinct collections, both presentations and R1 switching. */
#define main previous_ui_main
#include "ui_test.c"
#undef main
static const char *fixture="#EXTM3U\n#EXTINF:-1,First channel\nhttps://example.org/first.m3u8\n#EXTINF:-1,Second channel\nhttps://example.org/second.m3u8\n#EXTINF:-1,Third channel\nhttps://example.org/third.m3u8\n";
static unsigned captures,saw_saved_channels;
static void observe(unsigned tick){
 if(saved_view&&saved_channels){UiContextAction options[16];int n=options_for_page(options),found=0;for(int i=0;i<n;i++)if(options[i].action==20)found++;CHECK(found==1);saw_saved_channels++;for(unsigned i=0;i<result_count;i++)CHECK(catalog.items[results[i]].mode!=VT_IPTV_LIST);}
 if(tick==0){
  CHECK(saved_view&&!saved_channels&&playlist_count==2);
  UiContextAction options[16];int n=options_for_page(options),found=0;for(int i=0;i<n;i++)if(options[i].action==20)found++;CHECK(found==1);
  CHECK(result_count==(split_view?0u:2u));
  for(unsigned i=0;i<result_count;i++)CHECK(catalog.items[results[i]].mode==VT_IPTV_LIST);
 }
 if(!saved_view&&!captures){
  CHECK(result_count==3&&!strcmp(current_list.url,"https://example.org/list0.m3u"));
  CHECK(IPTV_CATALOG_Y==(split_view?152:116));
  for(int i=0;i<host_text_count;i++)CHECK(host_text_rects[i].y!=140); /* Removed context row. */
  CHECK(channel_list_x()==(split_view?340:18));
  char name[96];snprintf(name,sizeof(name),"iptv-browser-%d-%d-%u",split_view,ui_theme_id(),language);png(name);captures++;
 }
 host_text_count=0;
}
static void prepare(int split,const unsigned *keys,unsigned count){
 host_record_text=1;host_text_count=0;new_link();host_iptv_split=split;frame=script_count=axis_count=0;
 memset(input_script,0,sizeof(input_script));if(keys)memcpy(input_script,keys,count*sizeof(*keys));script_count=count;
 memset(&stored_channel,0,sizeof(stored_channel));stored_result=write_failure=online_case=0;
 input_fetches=plays=created=destroyed=0;input_list_fixture=fixture;last_stream_url[0]=0;
 next_result=next_event=0;mini_top=544;tap_frame=-1;captures=saw_saved_channels=0;frame_observer=observe;
}
static void switch_action(int split,int action){
 prepare(split,NULL,0);split_view=split;saved_view=1;saved_channels=0;
 CHECK(!reload_saved());picker_focus=split;picker_cursor=1;filter_catalog();
 UiContextAction actions[16];int n=options_for_page(actions),index=-1;
 for(int i=0;i<n;i++)if(actions[i].action==action)index=i;
 CHECK(index>=0);
 unsigned at=1;input_script[at++]=SCE_CTRL_RTRIGGER;at++;
 for(int i=0;i<index;i++){input_script[at++]=SCE_CTRL_DOWN;at++;}
 input_script[at++]=SCE_CTRL_CROSS;at+=18;
 input_script[at++]=SCE_CTRL_CIRCLE;at++;input_script[at++]=SCE_CTRL_CIRCLE;
 script_count=at;
 CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
 CHECK(!input_fetches&&!plays);
 if(action==22)CHECK(host_iptv_split==!split);
 else CHECK(saw_saved_channels);
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 host_saved_catalog=calloc(1,sizeof(*host_saved_catalog));CHECK(host_saved_catalog);host_saved_catalog->count=4;
 for(int i=0;i<4;i++){
  VtIptvChannel *c=&host_saved_catalog->items[i];c->mode=i<2?VT_IPTV_LIST:VT_IPTV_HLS;
  snprintf(c->url,sizeof(c->url),"https://example.org/%s%d.%s",i<2?"list":"saved",i,i<2?"m3u":"m3u8");
  snprintf(c->title,sizeof(c->title),"%s %d 日本語",i<2?"My playlist with a long name":"Saved channel",i);
 }
 const unsigned separate[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_RIGHT,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 const unsigned split[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_DOWN,0,SCE_CTRL_CROSS,0,SCE_CTRL_LEFT,0,SCE_CTRL_DOWN,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);
  prepare(0,separate,sizeof(separate)/sizeof(separate[0]));
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA&&input_fetches==1&&captures==1);
  CHECK(!strcmp(last_stream_url,"https://example.org/second.m3u8"));
  prepare(1,split,sizeof(split)/sizeof(split[0]));
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA&&input_fetches==2&&captures==1);
  CHECK(!strcmp(current_list.url,"https://example.org/list1.m3u"));
  switch_action(0,22);switch_action(1,22);switch_action(0,20);
 }
 frame_observer=NULL;free(host_saved_catalog);host_saved_catalog=NULL;
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("IPTV browser: %u checks; separate playlist/channel collections, two panes, both R1 layout switches, saved-channel action, D-pad open/play/back, preserved active catalogue, eight themes and IT/EN passed. Host pixels, not Vita runtime.\n",checks);
}
