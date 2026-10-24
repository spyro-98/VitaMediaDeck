/* Compile the production MP3 worker with real libmpg123 and host audio/I/O adapters. */
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <mpg123.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "media/background_playback.h"
#include "system/background_audio.h"
#define BACKGROUND_AUDIO_GRAIN 1024
#define SCE_O_RDONLY O_RDONLY
#define SCE_AUDIO_OUT_PORT_TYPE_BGM 1
#define SCE_AUDIO_OUT_MODE_STEREO 0
#define SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND 1
typedef int SceUID;
typedef int64_t SceOff;
typedef struct {
 char input_path[512];int remote_video_source;VtDecoderStreamHandle podcast_stream;
 uint64_t start_position_ms,seek_position_ms,podcast_position_ms;unsigned seek_serial;
 int cancel,desired_paused,podcast_position_valid;VtBackgroundPlaybackSnapshot snapshot;
} BackgroundPlaybackJob;
static BackgroundPlaybackJob *current;
static unsigned checks,outputs,fail_after,delay_calls;
static int seek_failure,paused_seek;
#define CHECK(x) do{checks++;assert(x);}while(0)
static void snapshot_lock(void){}
static void snapshot_unlock(void){}
static void player_lock(BackgroundPlaybackJob*j){(void)j;}
static void player_unlock(BackgroundPlaybackJob*j){(void)j;}
static uint64_t player_start_position(BackgroundPlaybackJob*j){return j->start_position_ms;}
static void set_state(VtBackgroundPlaybackState s,int e){current->snapshot.state=s;current->snapshot.error=e;}
static void publish_audio_metadata(BackgroundPlaybackJob*j,const char*c,uint32_t b,uint32_t r,uint16_t ch,uint16_t bits){(void)j;(void)c;(void)b;(void)bits;CHECK(r>0&&ch>0);}
static void set_port_volume(int port,int wanted,int *applied){CHECK(port==1&&wanted==100);*applied=wanted;}
static void amplify_pcm16(const int16_t *s,int16_t *d,uint32_t n,int p){(void)s;(void)d;(void)n;(void)p;}
int vt_audio_volume_percent(void){return 100;}
int vt_background_audio_acquire(VtBackgroundAudioLease*l){memset(l,0,sizeof(*l));return 0;}
int vt_background_audio_release(VtBackgroundAudioLease*l){(void)l;return 0;}
static int sceAudioOutOpenPort(int type,int grain,int rate,int mode){CHECK(type==1&&grain==1024&&rate>0&&mode==0);return 1;}
static int sceAudioOutReleasePort(int p){CHECK(p==1);return 0;}
static int sceAudioOutOutput(int p,const void *pcm){CHECK(p==1&&pcm);outputs++;if(fail_after&&outputs>=fail_after)return -77;if(outputs>=2)current->cancel=1;return 0;}
static int sceIoOpen(const char*p,int f,int m){return open(p,f,m);}
static int sceIoRead(int f,void*p,size_t n){return (int)read(f,p,n);}
static SceOff sceIoLseek(int f,SceOff off,int w){return lseek(f,off,w);}
static int sceIoClose(int f){return close(f);}
static uint64_t sceKernelGetProcessTimeWide(void){return 1000000;}
static int sceKernelPowerTick(int n){(void)n;return 0;}
static int sceKernelDelayThread(unsigned n){(void)n;delay_calls++;if(paused_seek&&delay_calls==1){current->seek_serial++;current->seek_position_ms=10000;}else current->cancel=1;return 0;}
static void log_printf(const char *s,...){(void)s;}
static off_t checked_seek(mpg123_handle*h,off_t sample,int w){if(seek_failure)return -1;return mpg123_seek(h,sample,w);}
#undef mpg123_seek
#define mpg123_seek checked_seek
#include "play-mp3.inc"
#undef mpg123_seek
static int stream_read(void *p,void*out,size_t n){return (int)fread(out,1,n,p);}
static int64_t stream_seek(void*p,int64_t offset,int whence){if(fseeko(p,offset,whence))return -1;return ftello(p);}
static void setup(BackgroundPlaybackJob *job,const char *path,int remote,uint64_t resume){
 memset(job,0,sizeof(*job));snprintf(job->input_path,sizeof(job->input_path),"%s",path);job->start_position_ms=resume;job->snapshot.position_ms=resume;
 job->remote_video_source=remote?2:0;job->snapshot.duration_ms=60000;current=job;outputs=fail_after=delay_calls=0;seek_failure=paused_seek=0;
 if(remote){FILE*f=fopen(path,"rb");CHECK(f);CHECK(!fseeko(f,0,SEEK_END));job->podcast_stream.size=ftello(f);rewind(f);job->podcast_stream.opaque=f;job->podcast_stream.read=stream_read;job->podcast_stream.seek=stream_seek;}
}
static void close_stream(BackgroundPlaybackJob*j){if(j->podcast_stream.opaque)fclose(j->podcast_stream.opaque);}
int main(int argc,char**argv){CHECK(argc==3);BackgroundPlaybackJob job;
 for(int file=1;file<argc;file++)for(int remote=0;remote<2;remote++){
  setup(&job,argv[file],remote,30000);CHECK(play_mp3(&job)==-1);CHECK(outputs==2&&job.podcast_position_valid&&job.podcast_position_ms>=30000&&job.podcast_position_ms<31000);close_stream(&job);
  setup(&job,argv[file],remote,30000);seek_failure=1;CHECK(play_mp3(&job)<0);CHECK(!outputs&&!job.podcast_position_valid&&job.snapshot.position_ms==30000);close_stream(&job);
  setup(&job,argv[file],remote,30000);fail_after=1;CHECK(play_mp3(&job)==-77);CHECK(outputs==1&&!job.podcast_position_valid);close_stream(&job);
  setup(&job,argv[file],remote,30000);fail_after=2;CHECK(play_mp3(&job)==-77);CHECK(outputs==2&&job.podcast_position_valid&&job.podcast_position_ms>=30000&&job.podcast_position_ms<31000);close_stream(&job);
  setup(&job,argv[file],remote,30000);job.desired_paused=paused_seek=1;CHECK(play_mp3(&job)==-1);CHECK(!outputs&&job.podcast_position_valid&&job.podcast_position_ms>=9900&&job.podcast_position_ms<=10100);close_stream(&job);
 }
 printf("Production MP3 resume: %u checks; real CBR/VBR, local/stream readers, 30-second restart, failed initial seek, paused rewind and audio-output failures passed\n",checks);return 0;
}
