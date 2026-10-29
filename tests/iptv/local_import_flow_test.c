/* Real import/page logic with controlled storage picker, fetch and player. */
#define VMD_PLAYLIST_MAIN prior_playlist_main
#include "playlist_flow_test.c"
#undef VMD_PLAYLIST_MAIN
static const char *single="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=4001642,CODECS=\"avc1.640028,mp4a.40.2\",RESOLUTION=1920x1080\nchunklist.m3u8\n";
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 const char *message="";
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);reset_flow(NULL,0);plays=created=destroyed=0;host_playlist_file="ux0:downloads/live.m3u";input_list_fixture=single;
  CHECK(import_local_file(&message)==UI_SECTION_NONE);CHECK(save_writes==1&&plays==1&&!input_dialog_calls);
  CHECK(stored_channel.mode==VT_IPTV_HLS&&!strcmp(stored_channel.url,host_playlist_file));
  CHECK(!strcmp(last_stream_url,host_playlist_file)&&!form_playlist);
  reset_flow(NULL,0);plays=created=destroyed=0;host_playlist_file="ux0:downloads/mixed.txt";
  input_list_fixture="https://example.org/channel.m3u8\nchild.m3u\nmore/lists.pls\n";
  CHECK(import_local_file(&message)==UI_SECTION_NONE);CHECK(!plays&&save_writes==1&&catalog.count==3);
  CHECK(stored_channel.mode==VT_IPTV_LIST&&!strcmp(catalog.items[1].url,"ux0:downloads/child.m3u"));
  CHECK(!strcmp(catalog.items[2].url,"ux0:downloads/more/lists.pls"));
  VtIptvChannel root=current_list;CHECK(push_list_parent(catalog.items[1].url,&message)&&list_parent_count==1);
  current_list=catalog.items[1];snprintf(link,sizeof(link),"%s",current_list.url);input_list_fixture=valid_list;
  CHECK(!open_list(&message)&&catalog.count==3);
  CHECK(!push_list_parent(root.url,&message)); /* Refuse a parent cycle. */
  current_list=list_parents[--list_parent_count];snprintf(link,sizeof(link),"%s",current_list.url);
  input_list_fixture="https://example.org/channel.m3u8\nchild.m3u\nmore/lists.pls\n";CHECK(!open_list(&message)&&catalog.count==3&&!list_parent_count);
  reset_flow(NULL,0);host_playlist_file="ux0:downloads/bad.txt";input_list_fixture="<html>Not a channel file</html>";
  CHECK(import_local_file(&message)==UI_SECTION_NONE&&!save_attempts&&!catalog.count);
  reset_flow(NULL,0);host_playlist_file=NULL;CHECK(import_local_file(&message)==UI_SECTION_NONE&&!input_fetches&&!save_attempts);
  reset_flow(NULL,0);write_failure=1;host_playlist_file="uma0:TV/list.m3u";input_list_fixture=valid_list;
  CHECK(import_local_file(&message)==UI_SECTION_NONE&&save_attempts==1&&!save_writes);
  /* The visible import button is reachable with arrows even with an empty URL. */
  reset_flow(NULL,0);host_playlist_file="ux0:downloads/mixed.txt";input_list_fixture=valid_list;
  const unsigned keys[]={0,SCE_CTRL_START,0,SCE_CTRL_DOWN,0,SCE_CTRL_DOWN,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
  frame_observer=snapshot;memcpy(input_script,keys,sizeof(keys));script_count=sizeof(keys)/sizeof(keys[0]);
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);CHECK(save_writes==1&&!input_dialog_calls);
  /* The user's HLS-with-.m3u case goes through QR Add without a second prompt. */
  const unsigned qr[]={0,SCE_CTRL_START,0,SCE_CTRL_TRIANGLE,0,SCE_CTRL_CROSS,0,SCE_CTRL_CIRCLE,0,SCE_CTRL_CIRCLE};
  reset_flow(qr,sizeof(qr)/sizeof(qr[0]));plays=created=destroyed=0;qr_value="https://example.org/live/playlist.m3u";input_list_fixture=single;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);CHECK(qr_calls==1&&save_writes==1&&plays==1&&!input_dialog_calls&&stored_channel.mode==VT_IPTV_HLS);
 }
 host_playlist_file=NULL;FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Local/channel-content import flow: %u checks; actual file import, single HLS with .m3u, mixed channel/list collection, nested/back/cycle, both mounts, invalid/cancel/write failures, visible D-pad import and QR Add in eight themes IT/EN passed.\n",checks);
}
