/* Production import/player metadata publication and cache restoration. */
#define main original_identity_ui_main
#include "ui_test.c"
#undef main
#include <unistd.h>
static void identity_capture(unsigned tick){
 if(tick==1){char filename[80];snprintf(filename,sizeof(filename),"channel-identity-%d-%u",ui_theme_id(),language);png(filename);}
}
static void reset_identity(void){
 new_link();memset(&stored_channel,0,sizeof(stored_channel));memset(&host_identity_library,0,sizeof(host_identity_library));
 saved_link_count=playlist_count=0;host_cached_art=1;save_attempts=save_writes=0;stored_result=write_failure=0;
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));const char *message="";
 const char *hls="#EXTM3U tvg-name=\"Example TV\" tvg-id=\"example.it\" tvg-logo=\"logo.png\"\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,Not a channel name\na.ts\n";
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);reset_identity();strcpy(link,"https://example.test/playlist.m3u");
  current_list=(VtIptvChannel){.mode=VT_IPTV_LIST,.name_source=VT_IPTV_NAME_AUTO};strcpy(current_list.url,link);strcpy(current_list.title,"M3U channel list");input_list_fixture=hls;
  CHECK(!open_list(&message));CHECK(current_list.mode==VT_IPTV_HLS&&!strcmp(current_list.title,"Example TV")&&!strcmp(current_list.logo,"https://example.test/logo.png"));
  CHECK(!strcmp(catalog.items[0].artwork_path,"ux0:data/VitaMediaDeck/artwork/cached.png"));CHECK(!save_link(&current_list)&&stored_channel.mode==VT_IPTV_HLS);
  CHECK(!strcmp(channel_type_label(&stored_channel),TR(IPTV_SINGLE_CHANNEL)));
  /* Repair a saved pre-fix entry without changing its key, star or preference. */
  reset_identity();strcpy(link,"https://example.test/legacy.m3u");stored_channel=(VtIptvChannel){.mode=VT_IPTV_LIST,.starred=1,.preferred={.height=720}};strcpy(stored_channel.url,link);strcpy(stored_channel.title,"M3U channel list");current_list=stored_channel;
  CHECK(!open_list(&message));CHECK(save_writes==1&&stored_channel.mode==VT_IPTV_HLS&&stored_channel.starred&&stored_channel.preferred.height==720&&!strcmp(stored_channel.title,"Example TV"));
  reset_identity();strcpy(link,"https://example.test/unnamed.m3u");stored_channel=(VtIptvChannel){.mode=VT_IPTV_LIST};strcpy(stored_channel.url,link);strcpy(stored_channel.title,"M3U channel list");current_list=stored_channel;
  input_list_fixture="#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,A film title\na.ts\n";
  CHECK(!open_list(&message)&&stored_channel.mode==VT_IPTV_HLS&&strcmp(stored_channel.title,"M3U channel list")&&stored_channel.name_source==VT_IPTV_NAME_AUTO);
  /* Local single-entry descriptors inherit the channel's metadata and icon. */
  reset_identity();strcpy(link,"ux0:TV/channel.m3u");current_list=(VtIptvChannel){.mode=VT_IPTV_LIST,.name_source=VT_IPTV_NAME_AUTO};strcpy(current_list.url,link);strcpy(current_list.title,"channel.m3u");
  input_list_fixture="#EXTM3U\n#EXTINF:-1 tvg-logo=\"icon.png\",Local Example TV\nhttps://example.test/live.m3u8\n";
  CHECK(!open_list(&message)&&current_list.single_channel&&!strcmp(current_list.title,"Local Example TV")&&!strcmp(current_list.logo,"ux0:TV/icon.png"));
  CHECK(!save_link(&current_list));saved_view=1;saved_channels=1;CHECK(!reload_saved());filter_catalog();CHECK(result_count==1&&!playlist_count&&!vt_iptv_channel_collection(&catalog.items[0]));
  CHECK(!strcmp(channel_type_label(&catalog.items[0]),TR(IPTV_SINGLE_CHANNEL)));
  frame=0;script_count=5;memset(input_script,0,sizeof(input_script));input_script[2]=input_script[4]=SCE_CTRL_CIRCLE;frame_observer=identity_capture;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA);frame_observer=NULL;

  /* Programme metadata is published on the UI thread, preserving custom names. */
  VtIptvProgram program={.channel_name="Canonical Example TV",.channel_logo="https://example.test/canonical.png",.channel_id="example.it"};
  VtIptvChannel playing={.mode=VT_IPTV_HLS,.name_source=VT_IPTV_NAME_AUTO};strcpy(playing.url,"https://example.test/new.m3u8");strcpy(playing.title,"Channel · example.test");
  CHECK(!save_link(&playing));PlayerFavoriteContext context={.channel=&playing};player_identity_metadata(&context,&program);
  CHECK(!strcmp(playing.title,program.channel_name)&&!strcmp(stored_channel.logo,program.channel_logo)&&!strcmp(stored_channel.artwork_path,"ux0:data/VitaMediaDeck/artwork/cached.png"));
  strcpy(playing.title,"My channel");playing.name_source=VT_IPTV_NAME_CUSTOM;playing.logo[0]=0;player_identity_metadata(&context,&program);CHECK(!strcmp(playing.title,"My channel")&&playing.logo[0]);
  write_failure=1;playing.name_source=VT_IPTV_NAME_AUTO;strcpy(playing.title,"IPTV");unsigned writes=save_writes;player_identity_metadata(&context,&program);CHECK(!strcmp(playing.title,program.channel_name)&&save_writes==writes);write_failure=0;
  reset_identity();VtIptvChannel known={.mode=VT_IPTV_HLS};strcpy(known.url,"https://example.test/known.m3u8");
  host_identity_library.item.provider=VT_LIBRARY_IPTV;strcpy(host_identity_library.item.locator,known.url);strcpy(host_identity_library.item.title,"Known TV");strcpy(host_identity_library.item.artwork_url,"https://example.test/known.png");
  channel_library_metadata(&known);CHECK(!strcmp(known.title,"Known TV")&&!strcmp(known.logo,"https://example.test/known.png"));
  /* Metadata cache hits publish while the image network worker is busy. */
  catalog.count=result_count=1;catalog.items[0]=known;catalog_top=0;catalog_visible=1;results[0]=0;artwork_thread=42;artwork_done=0;iptv_cover_tick();CHECK(!strcmp(catalog.items[0].artwork_path,"ux0:data/VitaMediaDeck/artwork/cached.png"));artwork_thread=-1;
 }
 char previous_directory[4096],temp[]="/tmp/vmd-identity-icon.XXXXXX";CHECK(getcwd(previous_directory,sizeof(previous_directory))&&mkdtemp(temp)&&!chdir(temp));
 char path[]="ux0:vmd-local-icon-fixture.png";FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite("PNG fixture",1,11,f)==11);CHECK(!fclose(f));size_t size=0;volatile int cancel=0;
 char *data=iptv_local_logo(path,&cancel,&size);CHECK(data&&size==11&&!memcmp(data,"PNG fixture",11));free(data);cancel=1;CHECK(!iptv_local_logo(path,&cancel,&size));CHECK(!remove(path));CHECK(!iptv_local_logo("ur0:private",NULL,&size));CHECK(!chdir(previous_directory)&&!rmdir(temp));
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Channel identity flow: %u checks; raw HLS/local one-channel imports, legacy repair, stars/quality retained, explicit names, unique library metadata, XMLTV publication, saved-channel grouping, cached-logo restore while busy, bounded local image IO and eight themes IT/EN passed.\n",checks);
}
