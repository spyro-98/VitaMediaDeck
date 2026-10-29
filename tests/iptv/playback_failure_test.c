/* Actual play_link transactions, error recovery dialog and fresh sessions. */
#define main original_ui_main
#include "ui_test.c"
#undef main
static int scenario,runs;
static int failing_player(const VtHwPlayerScreenSource *source){
 runs++;CHECK(runs<5);
 VtIptvOptions *o=(void*)source->iptv_options;VtIptvSelection *s=source->iptv_selection;
 int high=!strcmp(s->video,o->video[1].id);
 if(scenario<2){
  if(runs==1){CHECK(!source->iptv_started(source->iptv_started_ctx,480,280));strcpy(s->video,o->video[1].id);s->video_manual=1;return VT_HW_PLAYER_ACTION_STREAM_CHANGE;}
  if(runs==2){CHECK(high);if(scenario==1)CHECK(!source->iptv_started(source->iptv_started_ctx,1920,1080));next_event=VT_IPTV_VIDEO_CODEC;return -1;}
  CHECK(!high&&host_playback_present&&host_playback_preference.height==280);next_event=0;return 0;
 }
 if(scenario==2||scenario==3||scenario==6){
  if(runs==1){CHECK(high);next_event=VT_IPTV_VIDEO_CODEC;return -1;}
  CHECK(!s->video_manual&&!s->video[0]&&host_playback_preference.height==1080);
  if(scenario==3){next_event=VT_IPTV_MEDIA_DATA;return -1;}
  next_event=0;CHECK(!source->iptv_started(source->iptv_started_ctx,480,280));return 0;
 }
 if(scenario==5)CHECK(!source->iptv_started(source->iptv_started_ctx,480,280));
 next_event=scenario==4?VT_IPTV_TLS:scenario==7?0:VT_IPTV_IO;return -1;
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 host_quality_variants=1;live_player_observer=failing_player;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(scenario=0;scenario<8;scenario++){
  ui_theme_apply(theme);new_link();mode=VT_IPTV_HLS;strcpy(link,"https://fixture.test/channel.m3u8");
  memset(&stored_channel,0,sizeof(stored_channel));host_playback_present=scenario==2||scenario==3||scenario==4||scenario==6||scenario==7;
  host_playback_preference=(VtIptvPreference){.width=1920,.height=1080};
  host_playback_writes=runs=created=destroyed=0;next_event=next_result=0;frame=0;
  unsigned script[]={0,SCE_CTRL_RIGHT,0,SCE_CTRL_CROSS};memcpy(input_script,script,sizeof(script));script_count=4;
  if(scenario==6){input_script[1]=SCE_CTRL_CIRCLE;script_count=2;}
  const char *message="";CHECK(play_link(&message)==UI_SECTION_NONE&&created==destroyed);
  if(scenario<2)CHECK(runs==3&&host_playback_preference.height==280);
  if(scenario==2)CHECK(runs==2&&host_playback_preference.height==280&&host_playback_writes==1);
  if(scenario==3||scenario==4||scenario==6)CHECK(runs==(scenario==3?2:1)&&host_playback_preference.height==1080&&!host_playback_writes);
  if(scenario==5)CHECK(runs==1&&!host_playback_present);
  if(scenario==7)CHECK(runs==1&&!host_playback_writes&&host_playback_preference.height==1080&&message==TR(IPTV_NETWORK_ERROR));
  if(scenario==2){char name[80];snprintf(name,sizeof(name),"iptv-quality-recovery-%d-%u",theme,language);png(name);}
 }
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Playback failure recovery: %u checks; early/late failed switch rollback, first-choice removal after late failure, explicit one-shot Automatic retry, failed recovery/cancel retain saved preference, TLS does not retry, all themes IT/EN and joined fresh sessions passed.\n",checks);
}
