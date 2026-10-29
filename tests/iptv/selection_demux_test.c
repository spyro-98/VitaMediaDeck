#define main prior_demux_main
#include "demux_test.c"
#undef main
#include "network/hls_source.h"
#include "ui/iptv_player_panel.h"
int main(int argc,char **argv){
 CHECK(argc==2);fixture=argv[1];av_log_set_level(AV_LOG_ERROR);
 VtIptvOptions *o=calloc(1,sizeof(*o));CHECK(o);VtIptvSourceConfig config={0};volatile int cancel=0;
 const char *url="https://fixture.test/index.m3u8";
 CHECK(!vt_iptv_options_load(url,0,&config,o,&cancel));CHECK(o->video_count==2&&o->audio_count==2);
 for(int attempt=0;attempt<6;attempt++){
  config=(VtIptvSourceConfig){.buffer_limit=24u*1024u*1024u,.segment_limit=16u*1024u*1024u,.startup_segments=6,.stream_options=o};
  if(attempt==3){url="https://fixture.test/embedded.m3u8";CHECK(!vt_iptv_options_load(url,0,&config,o,&cancel));CHECK(!o->audio_count&&!o->video_count);}
  if(attempt==4){CHECK(o->audio_count==2);fprintf(stderr,"Embedded audio: %s/%s, %s/%s\n",o->audio[0].id,o->audio[0].language,o->audio[1].id,o->audio[1].language);CHECK(!strcmp(o->audio[1].language,"eng"));CHECK(ui_iptv_choice_apply(o,&config.selection,1,1));}
  if(attempt==5){url="ux0:downloads/index.m3u8";CHECK(!vt_iptv_options_load(url,0,&config,o,&cancel));CHECK(o->video_count==2&&o->audio_count==2);}
  if(attempt==1||attempt==2){CHECK(ui_iptv_choice_apply(o,&config.selection,0,1));CHECK(ui_iptv_choice_apply(o,&config.selection,1,attempt-1));}
  vt_hls_source_configure(&config);VtIptvSession *session=vt_iptv_session_create(url,&config);CHECK(session);
  VtIptvSourceStatus status;int64_t deadline=av_gettime_relative()+20000000;
  do{vt_iptv_session_status(session,&status);CHECK(status.terminal>=0&&av_gettime_relative()<deadline);if(!status.startup_ready)av_usleep(5000);}while(!status.startup_ready);
  if(attempt==3||attempt==4){CHECK(o->audio_count==2);CHECK(!strcmp(o->audio[0].language,"ita")&&!strcmp(o->audio[1].language,"eng"));}
  if(attempt<3){CHECK(o->active.video[0]&&o->active_height==((attempt==1||attempt==2)?1080:360));
   CHECK(!strcmp(o->active.video,(attempt==1||attempt==2)?o->video[1].id:o->video[0].id));
  }else if(attempt<5)CHECK(o->active.audio[0]);
  CHECK(o->channel_name[0]); /* SDT is recovered from the already-fetched first TS prefix. */
  CHECK(status.audio_sample_rate==(unsigned)((attempt==2||attempt==4)?44100:48000));
  VtDecoderStreamFactory factory;vt_iptv_session_factory(session,&factory);
  Input video={0},audio={0};open_input(&video,&factory);open_input(&audio,&factory);
  int64_t origin=video.format->start_time;Decode v={0},a={0};decoder_open(&v,&video,AVMEDIA_TYPE_VIDEO,origin);decoder_open(&a,&audio,AVMEDIA_TYPE_AUDIO,origin);
  CHECK(v.codec->height==((attempt==1||attempt==2)?1080:360)&&v.codec->width==((attempt==1||attempt==2)?1920:640));CHECK(a.codec->sample_rate==((attempt==2||attempt==4)?44100:48000));
  int ve=0,ae=0;while(!ve||!ae){if(!ve&&(ae||v.last<=a.last))ve=!decode_step(&v,&video);else if(!ae)ae=!decode_step(&a,&audio);}
  CHECK(v.frames==150&&a.frames>250);CHECK(llabs(v.last-a.last)<150000);CHECK(!vt_iptv_session_event(session));
  close_input(&audio);close_input(&video);vt_iptv_session_destroy(session);
  av_frame_free(&a.frame);av_frame_free(&v.frame);avcodec_free_context(&a.codec);avcodec_free_context(&v.codec);
 }
 free(o);printf("HLS actual selection/remux/decode: %u checks; Auto 360p/Italian, 1080p at 9Mbps/Italian 1080p at 9Mbps/English and embedded Italian/English decoded 150 video frames each; independent sample rates, A/V clocks and local-mount HLS addresses verified.\n",checks);
}
