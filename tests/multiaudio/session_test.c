/* Exercise the real video-session owner with the single real audio engine and
 * codec modules. Only video presentation, Vita firmware and kernel are mocked. */
#define main pipeline_regression_main
#define media_timeline_origin_us audio_timeline_origin_us
#include "pipeline_test.c"
#undef media_timeline_origin_us
#undef main
#ifdef TEST_HW
#include "../../../vita-hw-video-decoder/src/vita_hw_video_decoder.c"
#define session_create vita_hw_video_decoder_create
#define session_open vita_hw_video_decoder_open
#define session_close vita_hw_video_decoder_destroy
#define session_select vita_hw_video_decoder_select_audio_track
#define session_seek vita_hw_video_decoder_seek
#define session_stop vita_hw_video_decoder_request_stop
#define session_volume vita_hw_video_decoder_set_volume
#define SessionConfig VitaHwVideoDecoderPlayerConfig
#define SessionStream VitaHwVideoDecoderStreamHandle
#define SessionPlayer VitaHwVideoDecoderPlayer
#define video_start vita_hw_software_video_start
#define video_join vita_hw_software_video_join
#define video_restart vita_hw_software_video_join_for_restart
#define video_fence vita_hw_software_video_join_after_gpu_fence
#define video_buffers vita_hw_software_video_buffer_status
int vita_hw_video_decoder_runtime_prepare(void){return 0;}
#else
#include "../../../vita-sw-video-decoder/src/vita_sw_video_decoder.c"
#define session_create vita_sw_video_decoder_create
#define session_open vita_sw_video_decoder_open
#define session_close vita_sw_video_decoder_destroy
#define session_select vita_sw_video_decoder_select_audio_track
#define session_seek vita_sw_video_decoder_seek
#define session_stop vita_sw_video_decoder_request_stop
#define session_volume vita_sw_video_decoder_set_volume
#define SessionConfig VitaSwVideoDecoderPlayerConfig
#define SessionStream VitaSwVideoDecoderStreamHandle
#define SessionPlayer VitaSwVideoDecoderPlayer
#define video_start vita_sw_software_video_start
#define video_join vita_sw_software_video_join
#define video_restart vita_sw_software_video_join_for_restart
#define video_fence vita_sw_software_video_join_after_gpu_fence
#define video_buffers vita_sw_software_video_buffer_status
int vita_sw_video_decoder_runtime_prepare(void){return 0;}
#endif
static unsigned video_starts,cursors,opens,closes,driver_starts;
static int reject_next;
static int session_audio_start(VitaAudioPlayback *state,AVFormatContext *format,int index,uint64_t position,volatile int *cancel,volatile int *gate){
 driver_starts++;
 if(reject_next){reject_next=0;return AVERROR_INVALIDDATA;}
 native_parameters=format->streams[index]->codecpar;
 return playback_start(state,format,index,position,cancel,gate);
}
static const VitaAudioPlaybackDriver tracked_driver={session_audio_start,playback_join,playback_set_volume};
static int read_cursor(void *opaque,void *buffer,size_t bytes){FILE *f=opaque;size_t count=fread(buffer,1,bytes,f);return count?(int)count:feof(f)?0:-1;}
static int64_t seek_cursor(void *opaque,int64_t offset,int whence){FILE *f=opaque;if(whence==AVSEEK_SIZE){off_t now=ftello(f);fseeko(f,0,SEEK_END);off_t n=ftello(f);fseeko(f,now,SEEK_SET);return n;}if(fseeko(f,offset,whence&~AVSEEK_FORCE))return -1;return ftello(f);}
static void close_cursor(void *opaque){assert(cursors);fclose(opaque);cursors--;closes++;}
static int open_cursor(void *opaque,SessionStream *out){FILE *f=fopen(opaque,"rb");if(!f)return -1;fseeko(f,0,SEEK_END);off_t size=ftello(f);rewind(f);*out=(SessionStream){.opaque=f,.read=read_cursor,.seek=seek_cursor,.close=close_cursor,.size=size};cursors++;opens++;return 0;}
int video_start(SoftwareVideoState *state,AVFormatContext *format,int index,uint32_t width,uint32_t height,int fps,uint64_t position,volatile int *cancel,volatile int *gate,int64_t origin){
 (void)format;(void)index;(void)width;(void)height;(void)fps;(void)origin;
 memset(state,0,sizeof(*state));state->cancel=cancel;state->start_gate=gate;
 state->initial_position_ms=position;state->source_fps=24;state->started=1;state->frames_decoded=1;video_starts++;return 0;
}
void video_join(SoftwareVideoState *state){state->started=0;}
void video_restart(SoftwareVideoState *state){video_join(state);}
void video_fence(SoftwareVideoState *state){video_join(state);}
void video_buffers(SoftwareVideoState *state,int *ready,int *display,uint64_t *pts){if(ready)*ready=state->started?1:0;if(display)*display=state->started;if(pts)*pts=state->initial_position_ms*1000;}
void vita2d_wait_rendering_done(void){}
static void reset_evidence(void){assert(!cursors&&!port&&!library&&!block_count);thread_next=1;submitted=recorded=0;fail_native=0;video_starts=driver_starts=0;}
static void exercise_session(const char *path,int initial){
 reset_evidence();SessionPlayer *session=session_create();assert(session);volatile int cancel=0;
 SessionConfig config={.stream={.opaque=(void*)path,.open=open_cursor},.audio_track=initial,.start_paused=1,.volume_percent=75,.cancel_flag=&cancel,.audio_driver=&tracked_driver};
 assert(!session_open(session,&config));assert(session->has_audio&&cursors==2&&driver_starts==1&&video_starts==1);
 assert(session->audio.driver==&tracked_driver&&session->audio.paused&&session->audio.volume_percent==75);
 session_volume(session,300);assert(session->audio.volume_percent==300&&session->config.volume_percent==300);
 assert(session->audio_track_count==5&&session->audio_tracks[1].channels==6&&!strcmp(session->audio_tracks[1].language,"ita")&&!strcmp(session->audio_tracks[2].language,"rus"));
 assert(!session_select(session,1,1000));assert(session->config.audio_track==1&&session->has_audio&&video_starts==1&&session->audio.paused&&session->audio.volume_percent==300);
 assert(((AudioPlaybackEngine*)session->audio.implementation)->software);
 assert(!session_seek(session,3000));assert(session->config.audio_track==1&&session->has_audio&&video_starts==2&&session->audio.paused&&session->audio.volume_percent==300);
 assert(((AudioPlaybackEngine*)session->audio.implementation)->software);
 reject_next=1;
 assert(session_select(session,2,3000)==1);assert(session->config.audio_track==1&&session->has_audio&&video_starts==2&&session->audio.implementation);
 assert(!session_select(session,2,3000));assert(session->config.audio_track==2&&session->has_audio&&video_starts==2);
 assert(!((AudioPlaybackEngine*)session->audio.implementation)->software);
 assert(!session_seek(session,1000));assert(session->config.audio_track==2&&session->has_audio&&video_starts==3);
 assert(session->audio.driver==&tracked_driver&&session->audio.paused&&session->audio.volume_percent==300);
 session_stop(session);session_close(session);assert(!cursors&&!port&&!library&&!block_count&&opens==closes);
 puts("Real session owner + shared audio: AAC/AC3/MP3 switch, failed-switch rollback, bidirectional seek, language snapshots, pause/gain retention, cancellation and balanced cursor/decoder teardown passed.");
}
int main(int argc,char **argv){assert(argc==2);alarm(40);av_log_set_level(AV_LOG_ERROR);record_capacity=8*48000*2;recording=malloc(record_capacity*sizeof(int16_t));assert(recording);
 exercise_session(argv[1],0);exercise_session(argv[1],2);
 reset_evidence();SessionPlayer *silent=session_create();SessionConfig config={.stream={.opaque=argv[1],.open=open_cursor},.volume_percent=100};assert(!session_open(silent,&config));assert(!silent->has_audio&&cursors==1&&!driver_starts);assert(session_select(silent,1,0)==AVERROR(ENOSYS)&&cursors==1);session_close(silent);assert(!cursors&&opens==closes);free(recording);alarm(0);puts("Standalone video-only session: no audio driver, port or codec created; independent cursor closed.");return 0;}
