#define main original_ui_main
#include "ui_test.c"
#undef main
static unsigned trials,fail_trial;static int skip;
static int trial_player(const VtHwPlayerScreenSource *s){
 CHECK(s->quality_trial&&!s->favorite&&!s->history_id);trials++;
 next_event=0;
 if(trials==fail_trial){next_event=VT_IPTV_CANCELLED;return -1;}
 VtIptvOptions *o=(VtIptvOptions*)s->iptv_options;int index=!strcmp(s->iptv_selection->video,o->video[1].id);
 o->active=*s->iptv_selection;o->active_width=o->video[index].width;o->active_height=o->video[index].height;
 s->quality_trial->report=(VtIptvQualityReport){.elapsed_us=8000000,.media_us=8000000,.audio_us=8000000,.shown=200,.valid=1,.hardware=1,.stall_us=index?0:2000000};
 return skip?VT_HW_PLAYER_ACTION_TRIAL_SKIP:VT_HW_PLAYER_ACTION_TRIAL_DONE;
}
static int normal_runs,flow_mode;
static int explicit_player(const VtHwPlayerScreenSource *s){
 if(s->quality_trial)return trial_player(s);
 normal_runs++;
 if(flow_mode==0){CHECK(!trials&&normal_runs==1);return 0;}
 if(flow_mode==1){CHECK(!trials);return normal_runs==1?VT_HW_PLAYER_ACTION_STREAM_CHANGE:0;}
 if(normal_runs==1){
  CHECK(!trials);
  if(flow_mode==3){snprintf(s->iptv_selection->video,sizeof(s->iptv_selection->video),"%s",s->iptv_options->video[1].id);s->iptv_selection->video_manual=1;}
  return VT_HW_PLAYER_ACTION_RECALIBRATE;
 }
 CHECK(!strcmp(s->iptv_selection->video,s->iptv_options->video[1].id));
 CHECK(flow_mode!=3||s->iptv_selection->video_manual);
 return 0;
}
int main(void){
 VtIptvOptions *o=calloc(1,sizeof(*o));CHECK(o);o->video_count=2;
 for(unsigned i=0;i<2;i++){
  snprintf(o->video[i].id,sizeof(o->video[i].id),"https://fixture.test/%u.m3u8",i);
  o->video[i].width=i?1920:480;o->video[i].height=i?1080:280;o->video[i].supported=1;
 }
 VtIptvChannel channel={.url="https://fixture.test/master.m3u8",.mode=VT_IPTV_HLS};
 VtIptvSelection selection={0};VtIptvPreference recommended={0};live_player_observer=trial_player;
 CHECK(choose_smooth_quality(&channel,o,&selection,&recommended,1));
 CHECK(trials==2&&created==destroyed&&!selection.video_manual&&recommended.height==1080);
 CHECK(!strcmp(selection.video,o->video[1].id));
 int have_confirmed=1;VtIptvPreference confirmed;vt_iptv_preference_capture(o,&confirmed);
 PlayerFavoriteContext ctx={&channel,o,&confirmed,&have_confirmed};
 CHECK(!player_favorite_with_quality(&ctx,0));CHECK(player_favorite_with_quality(&ctx,1)==1);
 CHECK(stored_channel.starred&&stored_channel.preferred.height==1080&&stored_channel.preferred.video_key[0]);
 VtIptvPreference saved_preference=stored_channel.preferred;
 write_failure=1;CHECK(player_favorite_with_quality(&ctx,1)<0);CHECK(stored_channel.starred&&!memcmp(&saved_preference,&stored_channel.preferred,sizeof(saved_preference)));write_failure=0;
 CHECK(player_favorite_with_quality(&ctx,1)==0);CHECK(!stored_channel.starred);
 skip=1;selection=(VtIptvSelection){0};CHECK(!choose_smooth_quality(&channel,o,&selection,&recommended,1));
 CHECK(trials==3&&created==destroyed);
 skip=0;fail_trial=4;CHECK(choose_smooth_quality(&channel,o,&selection,&recommended,1));
 CHECK(trials==5&&created==destroyed&&recommended.height==1080);free(o);
 /* Real play_link startup, selecting Automatic, explicit R1 and cancellation. */
 host_quality_variants=1;fail_trial=0;memset(&stored_channel,0,sizeof(stored_channel));
 for(flow_mode=0;flow_mode<4;flow_mode++){
  new_link();snprintf(link,sizeof(link),"https://fixture.test/master.m3u8");mode=VT_IPTV_HLS;
  normal_runs=trials=plays=created=destroyed=0;next_event=next_result=0;skip=flow_mode==3;
  live_player_observer=explicit_player;const char *message="";
  CHECK(play_link(&message)==UI_SECTION_NONE&&created==destroyed);
  CHECK(trials==(flow_mode==2?2u:flow_mode==3?1u:0u));
 }
 printf("Manual quality integration: %u checks; no startup/Automatic probes, explicit R1 trials and cancelled-selection retention, actual active output preference, smooth 1080p selected over lagging 280p, fresh sessions and joined teardown, cancel, favourite persistence and failed-write rollback.\n",checks);
}
