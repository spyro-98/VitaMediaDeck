#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <vita_audio_playback.h>
const VitaAudioPlaybackDriver *vt_audio_playback_driver_for_mode(int mode){(void)mode;static const VitaAudioPlaybackDriver driver={0};return &driver;}
#include "../../src/media/vita_decoder.c"
static uint64_t landed,subtitle_position;
static int seek_result,subtitle_opens,subtitle_disabled,audio_result,audio_calls[3];
int vita_hw_video_decoder_select_audio_track_with_cancel(VitaHwVideoDecoderPlayer*p,int track,uint64_t ms,volatile int *cancel){(void)p;(void)ms;(void)cancel;assert(track>=0&&track<3);audio_calls[VT_DECODER_BACKEND_HARDWARE]++;return audio_result;}
int vita_sw_video_decoder_select_audio_track_with_cancel(VitaSwVideoDecoderPlayer*p,int track,uint64_t ms,volatile int *cancel){(void)p;(void)ms;(void)cancel;assert(track>=0&&track<3);audio_calls[VT_DECODER_BACKEND_SOFTWARE]++;return audio_result;}
int sceKernelDelayThread(unsigned us){(void)us;return 0;}
uint64_t sceKernelGetProcessTimeWide(void){return 1;}
void log_printf(const char *format,...){(void)format;}
int vita_hw_video_decoder_seek(VitaHwVideoDecoderPlayer*p,uint64_t ms){(void)p;(void)ms;return seek_result;}
int vita_sw_video_decoder_seek(VitaSwVideoDecoderPlayer*p,uint64_t ms){(void)p;(void)ms;return seek_result;}
void vita_hw_video_decoder_get_status(VitaHwVideoDecoderPlayer*p,VitaHwVideoDecoderPlayerStatus*s){(void)p;memset(s,0,sizeof(*s));s->position_ms=landed;s->duration_ms=100000;}
void vita_sw_video_decoder_get_status(VitaSwVideoDecoderPlayer*p,VitaSwVideoDecoderPlayerStatus*s){(void)p;memset(s,0,sizeof(*s));s->position_ms=landed;s->duration_ms=100000;}
void vt_subtitle_reader_tick(VtSubtitleReader*r){(void)r;}
void vt_subtitle_reader_disable(VtSubtitleReader*r){(void)r;subtitle_disabled++;}
int vt_subtitle_reader_open(VtSubtitleReader*r,const VtDecoderStreamFactory*f,int stream,uint64_t ms,volatile int*c){(void)r;(void)f;(void)c;assert(stream==7);subtitle_position=ms;subtitle_opens++;return 0;}
/* Reachable AUTO fallback symbols are inert; these tests force a backend. */
VitaSwVideoDecoderPlayer*vita_sw_video_decoder_create(void){assert(0);return NULL;}
int vita_sw_video_decoder_open(VitaSwVideoDecoderPlayer*p,const VitaSwVideoDecoderPlayerConfig*c){(void)p;(void)c;assert(0);return -1;}
void vita_hw_video_decoder_destroy(VitaHwVideoDecoderPlayer*p){(void)p;assert(0);}
int vita_sw_video_decoder_audio_track_count(const VitaSwVideoDecoderPlayer*p){(void)p;return 0;}
int vita_sw_video_decoder_subtitle_track_count(const VitaSwVideoDecoderPlayer*p){(void)p;return 0;}
int vita_sw_video_decoder_audio_track_info(const VitaSwVideoDecoderPlayer*p,int i,VitaSwVideoDecoderTrackInfo*t){(void)p;(void)i;(void)t;return -1;}
int vita_sw_video_decoder_subtitle_track_info(const VitaSwVideoDecoderPlayer*p,int i,VitaSwVideoDecoderTrackInfo*t){(void)p;(void)i;(void)t;return -1;}
int main(void){
 for(int backend=VT_DECODER_BACKEND_HARDWARE;backend<=VT_DECODER_BACKEND_SOFTWARE;backend++){
  VtDecoderPlayer p={0};p.backend=backend;p.config.preferred_backend=backend;
  p.hardware=(void*)&p;p.software=(void*)&p;p.subtitles=(void*)&p;
  p.active_subtitle_track=1;p.subtitle_track_count=1;p.subtitle_tracks[0].stream_index=7;
  p.audio_track_count=3;p.active_audio_track=0;audio_result=0;
  strcpy(p.audio_tracks[0].codec,"aac");strcpy(p.audio_tracks[1].codec,"ac3");strcpy(p.audio_tracks[2].codec,"mp3");
  int calls=audio_calls[backend],other=audio_calls[backend==VT_DECODER_BACKEND_HARDWARE?VT_DECODER_BACKEND_SOFTWARE:VT_DECODER_BACKEND_HARDWARE];
  for(int track=1;track<=2;track++){assert(!vt_decoder_select_audio_track(&p,track,1500));assert((int)p.backend==backend&&p.active_audio_track==track);}
  assert(audio_calls[backend]==calls+2&&audio_calls[backend==VT_DECODER_BACKEND_HARDWARE?VT_DECODER_BACKEND_SOFTWARE:VT_DECODER_BACKEND_HARDWARE]==other);
  audio_result=VT_DECODER_AUDIO_CHANGE_ROLLED_BACK;assert(vt_decoder_select_audio_track(&p,1,1500)==VT_DECODER_AUDIO_CHANGE_ROLLED_BACK&&p.active_audio_track==2&&(int)p.backend==backend);
  seek_result=0;landed=99750;int before=subtitle_opens;
  assert(!vt_decoder_seek(&p,100000));assert(subtitle_opens==before+1);
  assert(subtitle_position==99750&&p.active_subtitle_track==1);
  landed=4250;assert(!vt_decoder_seek(&p,4250));assert(subtitle_position==4250);
  seek_result=-5;before=subtitle_opens;assert(vt_decoder_seek(&p,12000)<0);assert(subtitle_opens==before);
  seek_result=0;p.active_subtitle_track=0;before=subtitle_disabled;
  assert(!vt_decoder_seek(&p,0));assert(subtitle_disabled==before+1&&!p.active_subtitle_track);
 }
 puts("Production player seek adapter: HW/SW landed position, subtitle choice, Off, failed seeks, AAC/AC3/MP3 audio changes without video-backend fallback and failed-change rollback passed.");
}
