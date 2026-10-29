/* Production channel open/change/close flow with independent stored choices. */
#define main original_ui_main
#include "ui_test.c"
#undef main
static int scenario,runs;
static int channel_player(const VtHwPlayerScreenSource *source){
 CHECK(!source->quality_trial);runs++;
 VtIptvOptions *o=(VtIptvOptions*)source->iptv_options;
 VtIptvSelection *s=source->iptv_selection;
 if(scenario<4){
  CHECK(s->video_manual==(scenario==0||scenario==2));
  CHECK((scenario==0||scenario==2)?!strcmp(s->video,o->video[1].id):!s->video[0]);
  CHECK((scenario==0||scenario==1)?!strcmp(s->audio,"audio-it"):!s->audio[0]);
 }
 int video=!strcmp(s->video,o->video[1].id);
 o->active=*s;o->active_width=o->video[video].width;o->active_height=o->video[video].height;
 if(!o->active.video[0])strcpy(o->active.video,o->video[video].id);
 if(!o->active.audio[0])strcpy(o->active.audio,"audio-en");
 CHECK(source->iptv_started);
 CHECK(!source->iptv_started(source->iptv_started_ctx,o->active_width,o->active_height));
 if(scenario==4)CHECK(host_playback_present&&host_playback_preference.height==280); /* Before player exit. */
 if(scenario==5&&runs==2)CHECK(host_playback_preference.height==1080); /* Before another input/exit. */
 if(scenario==5&&runs==1){
  strcpy(s->video,o->video[1].id);strcpy(s->audio,"audio-it");s->video_manual=1;
  return VT_HW_PLAYER_ACTION_STREAM_CHANGE;
 }
 return 0;
}
int main(void){
 host_quality_variants=host_playback_audio=1;live_player_observer=channel_player;
 for(scenario=0;scenario<6;scenario++){
  new_link();strcpy(link,"https://fixture.test/channel.m3u8");mode=VT_IPTV_HLS;
  memset(&stored_channel,0,sizeof(stored_channel));
  host_playback_preference=(VtIptvPreference){.width=1920,.height=scenario==1||scenario==3?720:1080};
  strcpy(host_playback_preference.audio_language,scenario==2||scenario==3?"fra":"ita");
  if(scenario==5){host_playback_preference.width=480;host_playback_preference.height=280;strcpy(host_playback_preference.audio_language,"eng");}
  host_playback_present=scenario!=4;host_playback_writes=runs=created=destroyed=0;
  VtIptvPreference original=host_playback_preference;
  const char *message="";CHECK(play_link(&message)==UI_SECTION_NONE&&created==destroyed);
  CHECK(!stored_channel.url[0]); /* Does not create or change a favourite. */
  if(scenario<4)CHECK(host_playback_writes==0&&!memcmp(&original,&host_playback_preference,sizeof(original)));
  if(scenario==4)CHECK(host_playback_writes==1&&host_playback_preference.height==280&&!host_playback_preference.audio_language[0]);
  if(scenario==5)CHECK(host_playback_writes==1&&runs==2&&host_playback_preference.height==1080&&!strcmp(host_playback_preference.audio_language,"ita"));
 }
 printf("Per-channel playback: %u checks; independent video/audio restoration, missing-field defaults, temporary fallback retention, non-favourite persistence and explicit-change commit passed.\n",checks);
}
