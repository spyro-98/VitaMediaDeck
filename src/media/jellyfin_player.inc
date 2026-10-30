#include "network/jellyfin_playback.h"
#include "network/hls_source.h"
#include "network/iptv_session.h"
typedef struct {
 const UiNetworkSelection *selection;
 VtJellyfinPlayback playback;
 uint64_t position;
 int force;
 volatile int cancel;
} JellyfinPlaybackTask;
static int jellyfin_negotiate_task(void *opaque){
 JellyfinPlaybackTask *t=opaque;
 return vt_jellyfin_playback_prepare(&t->selection->source,&t->selection->credential,
  t->selection->path,t->position,t->force,&t->playback,&t->cancel);
}
static int jellyfin_stop_task(void *opaque){
 JellyfinPlaybackTask *t=opaque;
 return vt_jellyfin_playback_stop(&t->selection->source,&t->selection->credential,&t->playback,t->position);
}
static void jellyfin_stop_present(JellyfinPlaybackTask *task){
 /* Cleanup is mandatory even when its spinner is dismissed. The HTTP request
  * has its own short timeout and the loader joins its worker before returning. */
 UiPlayerLoadingInfo closing={.title=task->selection->title,.status=vt_i18n_str(VT_STR_IPTV_CLOSING)};
 volatile int cancel=0;
 int result=ui_player_loading_run(&closing,jellyfin_stop_task,task,&cancel,NULL,NULL);
 if(result==UI_LOADING_TASK_FAILED)jellyfin_stop_task(task);
}
static int run_remote_video_fullscreen(const UiNetworkSelection *selection,uint64_t start_position_ms,
 int start_audio_track,int start_subtitle_track,uint64_t *last_position_ms,uint64_t *last_duration_ms,
 int *last_audio_track,int *last_subtitle_track,int *paused){
 if(selection->source.protocol!=VT_NETWORK_JELLYFIN)
  return run_remote_video_direct(selection,start_position_ms,start_audio_track,start_subtitle_track,
   last_position_ms,last_duration_ms,last_audio_track,last_subtitle_track,paused);
 JellyfinPlaybackTask task={.selection=selection,.position=start_position_ms};
 VtDecoderBackend backend=VT_DECODER_BACKEND_NONE;
 uint64_t position=start_position_ms,duration=selection->has_jellyfin_metadata?selection->jellyfin_metadata.runtime_ms:0;
 for(;;){
  task.cancel=0;
  UiPlayerLoadingInfo loading={.title=selection->title,.channel=selection->source.name,
                              .status=vt_i18n_str(VT_STR_PLAYER_OPEN_REMOTE)};
  int result=ui_player_loading_run(&loading,jellyfin_negotiate_task,&task,&task.cancel,NULL,NULL);
  if(task.cancel)return 0;
  if(result<0)return result;
  if(!task.playback.transcoding){
   UiNetworkSelection direct=*selection;
   snprintf(direct.jellyfin_metadata.media_source_id,sizeof(direct.jellyfin_metadata.media_source_id),"%s",task.playback.media_source_id);
   direct.has_jellyfin_metadata=1;
   result=run_remote_video_direct(&direct,task.position,start_audio_track,start_subtitle_track,
    &position,&duration,last_audio_track,last_subtitle_track,paused);
   memset(&direct.credential,0,sizeof(direct.credential));
   task.playback.session_id[0]=0;
   if(result<0){task.force=1;task.position=position;continue;}
   if(last_position_ms)*last_position_ms=position;
   if(last_duration_ms)*last_duration_ms=duration;
   return result;
  }
  vt_video_thumbnail_prepare_playback();
  vt_background_playback_stop();
  VtJellyfinHlsTransport transport={.source=&selection->source,.credential=&selection->credential};
  VtIptvSourceConfig config={.fetch=vt_jellyfin_hls_fetch,.fetch_range=vt_jellyfin_hls_fetch_range,
   .fetch_ctx=&transport,.buffer_limit=12u*1024u*1024u,.segment_limit=8u*1024u*1024u,.startup_segments=2};
  vt_hls_source_configure(&config);
  VtIptvSession *session=vt_iptv_session_create(task.playback.url,&config);
  if(!session){jellyfin_stop_present(&task);return -1;}
  char id[16];remote_media_id(selection,id);
  VtNetworkEntry entry={.is_video=1};snprintf(entry.path,sizeof(entry.path),"%s",selection->path);
  snprintf(entry.name,sizeof(entry.name),"%s",selection->title);
  VtLibraryItem library_item;vt_library_remote(&selection->source,&entry,NULL,&library_item);vt_library_bind(&library_item);
  uint64_t requested=task.position;
  if(task.playback.duration_ms)duration=task.playback.duration_ms;
  VtHwPlayerScreenSource source={.title=selection->title,.location=selection->source.name,.history_id=id,
   .authenticated_remote=1,.start_position_ms=task.position,.start_paused=paused?*paused:0,
   .timeline_offset_ms=task.position,.total_duration_ms=duration,.requested_seek_ms=&requested,.session_backend=&backend};
  vt_iptv_session_factory(session,&source.stream);
  result=vt_hw_player_screen_run_server(&source,session,&position,&duration,paused);
  /* The player has destroyed both decoder cursors. Only now may the producer
   * and remote encoder stop. Every seek receives a fresh PlaySessionId. */
  vt_iptv_session_cancel(session);
  UiPlayerLoadingInfo closing={.title=selection->title,.status=vt_i18n_str(VT_STR_IPTV_CLOSING)};
  while(!vt_iptv_session_stopped(session)){ui_player_loading_present(&closing,NULL,NULL);sceKernelDelayThread(16000);}
  vt_iptv_session_destroy(session);task.position=position;jellyfin_stop_present(&task);
  if(last_position_ms)*last_position_ms=position;
   if(last_duration_ms)*last_duration_ms=duration;
  if(last_audio_track)*last_audio_track=0;
  if(last_subtitle_track)*last_subtitle_track=0;
  if(result==VT_HW_PLAYER_ACTION_SERVER_SEEK){task.force=1;task.position=requested;continue;}
  if(result>=VT_HW_PLAYER_ACTION_SECTION_BASE&&result<VT_HW_PLAYER_ACTION_SECTION_BASE+UI_SECTION_ACTION_COUNT)
   return result-VT_HW_PLAYER_ACTION_SECTION_BASE+VT_HW_PLAYER_ACTION_SECTION_NO_MINIMIZE_BASE;
  return result;
 }
}
