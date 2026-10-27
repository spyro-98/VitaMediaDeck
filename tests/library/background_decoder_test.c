/* Real background decoder/guard lifecycle; platform/backend boundaries mocked. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "media/background_playback.h"
#include "media/vita_decoder.h"
#include "settings/preferences.h"
#include "system/background_audio.h"
#include "system/performance.h"
typedef struct {
 int remote_video_source,audio_track,subtitle_track,external_subtitle_count;
 char input_path[512];VtNetworkStreamFactory remote_factory;
 VtDecoderExternalSubtitle external_subtitles[VT_DECODER_MAX_SUBTITLE_TRACKS];
 volatile int cancel,desired_paused,local_video_source,video_in_gpu;
 unsigned seek_serial;uint64_t seek_position_ms,podcast_position_ms;
 int podcast_position_valid;VtDecoderPlayer *active_decoder;
 VtBackgroundPlaybackSnapshot snapshot;
} BackgroundPlaybackJob;
static int preference,create_failure,open_failure,cancel_open;
static int lease_active,guard_active,created,notices,destroyed,ended,backend_sequence[3];
static void snapshot_lock(void){}static void snapshot_unlock(void){}
static void player_lock(BackgroundPlaybackJob *j){(void)j;}static void player_unlock(BackgroundPlaybackJob *j){(void)j;}
static int player_lock_when_video_idle(BackgroundPlaybackJob *j,uint64_t t){(void)j;(void)t;return 1;}
static uint64_t player_start_position(BackgroundPlaybackJob*j){(void)j;return 43000;}
static void set_state(VtBackgroundPlaybackState s,int e){(void)s;(void)e;}
int vt_background_audio_acquire(VtBackgroundAudioLease*l){(void)l;lease_active=1;return 0;}
int vt_background_audio_release(VtBackgroundAudioLease*l){(void)l;assert(!guard_active);lease_active=0;return 0;}
void vt_performance_begin_video(VtPerformanceClockGuard*g,int h,int fps){(void)g;(void)h;(void)fps;assert(lease_active&&!guard_active);guard_active=1;}
void vt_performance_select_backend(VtPerformanceClockGuard*g,int hw){assert(g&&guard_active&&notices<3);backend_sequence[notices++]=hw;}
void vt_performance_end_video(VtPerformanceClockGuard*g){assert(g&&guard_active);if(created)assert(destroyed==1);guard_active=0;ended++;}
int vt_preferences_video_decoder(void){return preference;}
int vt_preferences_mini_player_animated(void){return 1;}
int vt_audio_volume_percent(void){return 100;}
VtDecoderPlayer *vt_decoder_create(void){if(create_failure)return NULL;assert(guard_active);created=1;return (void*)&created;}
void vt_decoder_destroy(VtDecoderPlayer*p){assert(p==(void*)&created&&guard_active);destroyed++;}
void vt_decoder_file_stream_factory(const char*p,VtDecoderStreamFactory*f){(void)p;memset(f,0,sizeof(*f));}
void vt_http_audio_stream_factory(const char*p,VtDecoderStreamFactory*f){vt_decoder_file_stream_factory(p,f);}
int vt_decoder_open(VtDecoderPlayer*p,const VtDecoderPlayerConfig*c){
 assert(p&&guard_active&&c->start_position_ms==43000&&c->backend_start&&c->backend_start_ctx);
 VtDecoderBackend expected=preference==VT_VIDEO_DECODER_HW_H264?VT_DECODER_BACKEND_HARDWARE:preference==VT_VIDEO_DECODER_SW_FFMPEG?VT_DECODER_BACKEND_SOFTWARE:VT_DECODER_BACKEND_NONE;
 assert(c->preferred_backend==expected);
 if(expected!=VT_DECODER_BACKEND_SOFTWARE)c->backend_start(c->backend_start_ctx,VT_DECODER_BACKEND_HARDWARE);
 if(expected!=VT_DECODER_BACKEND_HARDWARE)c->backend_start(c->backend_start_ctx,VT_DECODER_BACKEND_SOFTWARE);
 if(cancel_open)*c->cancel_flag=1;
 return open_failure?-7:0;
}
void vt_decoder_get_status(VtDecoderPlayer*p,VtDecoderPlayerStatus*s){(void)p;memset(s,0,sizeof(*s));s->opened=s->eof=1;s->position_ms=44000;s->duration_ms=60000;}
void vt_decoder_prepare_background_restart(VtDecoderPlayer*p){(void)p;}
void vt_decoder_set_paused(VtDecoderPlayer*p,int b){(void)p;(void)b;}
void vt_decoder_discard_video_to_clock(VtDecoderPlayer*p){(void)p;}
int vt_decoder_seek(VtDecoderPlayer*p,uint64_t n){(void)p;(void)n;return 0;}
uint64_t sceKernelGetProcessTimeWide(void){return 1000000;}
int sceKernelDelayThread(unsigned n){(void)n;return 0;}
int sceKernelPowerTick(int n){(void)n;return 0;}
#define SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND 0
#include "background-decoder.inc"
int main(void){
 for(int pref=0;pref<3;pref++)for(int mode=0;mode<4;mode++){
  BackgroundPlaybackJob job={0};preference=pref;
  create_failure=mode==1;open_failure=mode==2;cancel_open=mode==3;
  lease_active=guard_active=created=notices=destroyed=ended=0;
  int result=play_decoder_source(&job);
  assert(!guard_active&&!lease_active&&ended==1);
  if(mode==0){assert(result==0&&job.podcast_position_valid&&job.snapshot.position_ms==44000);}
  else assert(result<0);
  if(!create_failure){assert(destroyed==1);assert(notices==(pref==VT_VIDEO_DECODER_AUTO?2:1));assert(backend_sequence[notices-1]==(pref==VT_VIDEO_DECODER_HW_H264));}
  else assert(!notices&&!destroyed);
 }
 puts("Background decoder clocks: 12 actual lifecycle cases; saved AUTO/HW/SW, fallback callbacks, EOF, allocation/open failure, cancellation and guard lifetime through destruction passed.");
}
