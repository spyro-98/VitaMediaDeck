/* Actual UI event loop, input/name dialogs and M3U parser; network, player,
 * storage and firmware are controlled boundaries. No test stream is shipped. */
#define main previous_iptv_main
#include "ui_test.c"
#undef main
static const char *const valid_list="#EXTM3U\n#EXTINF:-1,First channel\nhttps://example.org/first.m3u8\n#EXTINF:-1,Second channel\nhttps://example.org/second.m3u8\n#EXTINF:-1,Third channel\nhttps://example.org/third.m3u8\n";
static void snapshot(unsigned tick){
 if(getenv("VMD_TRACE_FLOW"))fprintf(stderr,"frame=%u key=%u count=%u saved=%d picker=%d result=%u plays=%u writes=%u input=%u mode=%d\n",tick,input_script[tick],catalog.count,saved_view,picker_focus,result_count,plays,save_writes,input_dialog_calls,mode);
 if(tick==0||tick==2||tick==4||tick==6||tick==10){char name[96];snprintf(name,sizeof(name),"playlist-flow-%d-%u-step-%u",ui_theme_id(),language,tick);png(name);}
}
static void reset_flow(const unsigned *sequence,unsigned count){
 host_iptv_split=0;new_link();memset(&stored_channel,0,sizeof(stored_channel));
 saved_search[0]=saved_parent_url[0]=0;saved_parent_focus=saved_parent_top=0;
 frame=script_count=0;memset(input_script,0,sizeof(input_script));
 if(count)memcpy(input_script,sequence,count*sizeof(*sequence));script_count=count;
 rename_input=NULL;input_queue_count=1;input_queue_index=0;input_dialog_calls=qr_calls=0;qr_result=1;qr_value="https://example.org/list.m3u";
 input_queue[0]="https://example.org/channels?auth=secret-token";input_queue[1]="My TV";
 input_list_fixture=valid_list;input_fetches=save_attempts=save_writes=0;
 write_failure=stored_result=online_case=0;mini_top=544;tap_frame=-1;
 last_stream_url[0]=0;next_event=VT_IPTV_CANCELLED;next_result=0;
 direct_stream_labels=0;frame_observer=NULL;
}
static void retry_write(unsigned tick){
 if(tick==5){CHECK(!catalog.count&&!saved_view&&link[0]&&!form_link_saved&&save_attempts==1);write_failure=0;}
}
static int simulate_quality_change(const VtHwPlayerScreenSource *source){
 CHECK(source->iptv_options&&source->iptv_selection);
 if(plays==1){CHECK(!last_session_selection.video[0]);snprintf(source->iptv_selection->video,sizeof(source->iptv_selection->video),"chosen-quality");return VT_HW_PLAYER_ACTION_STREAM_CHANGE;}
 if(plays==2){CHECK(!strcmp(last_session_selection.video,"chosen-quality"));return -123;}
 CHECK(plays==3&&!last_session_selection.video[0]&&!source->iptv_selection->video[0]);return 0;
}
#ifndef VMD_PLAYLIST_MAIN
#define VMD_PLAYLIST_MAIN main
#endif
int VMD_PLAYLIST_MAIN(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));
 FT_Face inter,mono;CHECK(!FT_New_Face(ft,argv[1],0,&inter));
 CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));
 const unsigned add[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 const unsigned partial[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_RIGHT,0,SCE_CTRL_RIGHT,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 const unsigned cancel[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 const unsigned attempt[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 const unsigned qr_add[]={0,SCE_CTRL_START,0,SCE_CTRL_TRIANGLE,0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 const unsigned once[]={0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,SCE_CTRL_RIGHT,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 const unsigned options_keys[]={0,SCE_CTRL_UP,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;
  reset_flow(add,sizeof(add)/sizeof(add[0]));frame_observer=snapshot;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(input_queue_index==1&&input_dialog_calls==1&&input_fetches==1&&save_writes==1&&save_attempts==1);
  CHECK(stored_channel.mode==VT_IPTV_LIST&&strstr(stored_channel.title,"example.org"));
  CHECK(!strcmp(stored_channel.url,input_queue[0])&&!list_from_form&&form_link_saved);
  CHECK(!strcmp(last_stream_url,"https://example.org/first.m3u8"));
  reset_flow(partial,sizeof(partial)/sizeof(partial[0]));
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA&&save_writes==1);
  CHECK(!strcmp(last_stream_url,"https://example.org/third.m3u8"));
  reset_flow(cancel,sizeof(cancel)/sizeof(cancel[0]));input_queue[0]=NULL;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(input_queue_index==1&&input_dialog_calls==1&&!input_fetches&&!save_attempts&&!stored_channel.url[0]);
  reset_flow(attempt,sizeof(attempt)/sizeof(attempt[0]));input_list_fixture="<html>Not a playlist</html>";
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(input_fetches==1&&!save_attempts&&!stored_channel.url[0]&&!last_stream_url[0]);
  reset_flow(attempt,sizeof(attempt)/sizeof(attempt[0]));write_failure=1;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(input_fetches==1&&save_attempts==1&&!save_writes&&!stored_channel.url[0]&&!form_link_saved);
  reset_flow(once,sizeof(once)/sizeof(once[0]));input_queue_count=1;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(input_queue_index==1&&input_fetches==1&&!save_attempts&&!stored_channel.url[0]);
  for(int split=0;split<2;split++){
   reset_flow(qr_add,sizeof(qr_add)/sizeof(qr_add[0]));host_iptv_split=split;
   CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
   CHECK(qr_calls==1&&!input_dialog_calls&&!input_queue_index&&input_fetches==1&&save_writes==1&&form_link_saved);
   CHECK(!strcmp(stored_channel.url,qr_value)&&!strcmp(last_stream_url,"https://example.org/first.m3u8"));
  }
  const unsigned qr_cancel[]={0,SCE_CTRL_START,0,SCE_CTRL_TRIANGLE,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
  reset_flow(qr_cancel,sizeof(qr_cancel)/sizeof(qr_cancel[0]));qr_result=0;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(qr_calls==1&&!input_dialog_calls&&!input_fetches&&!save_attempts&&!link[0]);
  reset_flow(qr_cancel,sizeof(qr_cancel)/sizeof(qr_cancel[0]));qr_value="file:///invalid.m3u";
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(qr_calls==1&&!input_dialog_calls&&!input_fetches&&!save_attempts&&!link[0]);
  const unsigned retry[]={0,SCE_CTRL_START,0,SCE_CTRL_TRIANGLE,0,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
  reset_flow(retry,sizeof(retry)/sizeof(retry[0]));write_failure=1;frame_observer=retry_write;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(qr_calls==1&&!input_dialog_calls&&input_fetches==2&&save_attempts==2&&save_writes==1&&form_link_saved);
  reset_flow(options_keys,sizeof(options_keys)/sizeof(options_keys[0]));
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(direct_stream_labels&&!input_fetches&&!save_attempts);
 }
 reset_flow(NULL,0);plays=created=destroyed=0;next_event=0;new_link();snprintf(link,sizeof(link),"https://example.org/channel.m3u8");mode=VT_IPTV_HLS;
 const char *result_message="";live_player_observer=simulate_quality_change;
 CHECK(play_link(&result_message)==UI_SECTION_NONE);CHECK(plays==3&&created==3&&destroyed==3);live_player_observer=NULL;
 VtIptvChannel existing={.url="https://example.org/list.m3u",.title="A custom playlist name",.mode=VT_IPTV_LIST};
 stored_channel=existing;VtIptvChannel duplicate=existing;duplicate.title[0]=0;
 default_link_name(&duplicate);CHECK(!strcmp(duplicate.title,existing.title));
 frame_observer=NULL;FT_Done_Face(inter);FT_Done_Face(mono);FT_Done_FreeType(ft);
 printf("Playlist flow: %u checks; eight themes, IT/EN, D-pad/manual and QR add/save/open/play/back without a naming keyboard, both presentations, partial last row, cancellation, invalid input, failed writes, open-once reachable header options and failed-format rollback passed.\n",checks);return 0;
}
