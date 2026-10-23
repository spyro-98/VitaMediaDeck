#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../../src/media/vita_decoder.c"
static uint64_t landed,subtitle_position;
static int seek_result,subtitle_opens,subtitle_disabled;
int sceKernelDelayThread(unsigned us){(void)us;return 0;}
uint64_t sceKernelGetProcessTimeWide(void){return 1;}
void log_printf(const char *format,...){(void)format;}
int vita_hw_decoder_seek(VitaHwDecoderPlayer*p,uint64_t ms){(void)p;(void)ms;return seek_result;}
int vita_sw_decoder_seek(VitaSwDecoderPlayer*p,uint64_t ms){(void)p;(void)ms;return seek_result;}
void vita_hw_decoder_get_status(VitaHwDecoderPlayer*p,VitaHwDecoderPlayerStatus*s){(void)p;memset(s,0,sizeof(*s));s->position_ms=landed;s->duration_ms=100000;}
void vita_sw_decoder_get_status(VitaSwDecoderPlayer*p,VitaSwDecoderPlayerStatus*s){(void)p;memset(s,0,sizeof(*s));s->position_ms=landed;s->duration_ms=100000;}
void vt_subtitle_reader_tick(VtSubtitleReader*r){(void)r;}
void vt_subtitle_reader_disable(VtSubtitleReader*r){(void)r;subtitle_disabled++;}
int vt_subtitle_reader_open(VtSubtitleReader*r,const VtDecoderStreamFactory*f,int stream,uint64_t ms,volatile int*c){(void)r;(void)f;(void)c;assert(stream==7);subtitle_position=ms;subtitle_opens++;return 0;}
/* Reachable AUTO fallback symbols are inert; these tests force a backend. */
VitaSwDecoderPlayer*vita_sw_decoder_create(void){assert(0);return NULL;}
int vita_sw_decoder_open(VitaSwDecoderPlayer*p,const VitaSwDecoderPlayerConfig*c){(void)p;(void)c;assert(0);return -1;}
void vita_hw_decoder_destroy(VitaHwDecoderPlayer*p){(void)p;assert(0);}
int vita_sw_decoder_audio_track_count(const VitaSwDecoderPlayer*p){(void)p;return 0;}
int vita_sw_decoder_subtitle_track_count(const VitaSwDecoderPlayer*p){(void)p;return 0;}
int vita_sw_decoder_audio_track_info(const VitaSwDecoderPlayer*p,int i,VitaSwDecoderTrackInfo*t){(void)p;(void)i;(void)t;return -1;}
int vita_sw_decoder_subtitle_track_info(const VitaSwDecoderPlayer*p,int i,VitaSwDecoderTrackInfo*t){(void)p;(void)i;(void)t;return -1;}
int main(void){
 for(int backend=VT_DECODER_BACKEND_HARDWARE;backend<=VT_DECODER_BACKEND_SOFTWARE;backend++){
  VtDecoderPlayer p={0};p.backend=backend;p.config.preferred_backend=backend;
  p.hardware=(void*)&p;p.software=(void*)&p;p.subtitles=(void*)&p;
  p.active_subtitle_track=1;p.subtitle_track_count=1;p.subtitle_tracks[0].stream_index=7;
  seek_result=0;landed=99750;int before=subtitle_opens;
  assert(!vt_decoder_seek(&p,100000));assert(subtitle_opens==before+1);
  assert(subtitle_position==99750&&p.active_subtitle_track==1);
  landed=4250;assert(!vt_decoder_seek(&p,4250));assert(subtitle_position==4250);
  seek_result=-5;before=subtitle_opens;assert(vt_decoder_seek(&p,12000)<0);assert(subtitle_opens==before);
  seek_result=0;p.active_subtitle_track=0;before=subtitle_disabled;
  assert(!vt_decoder_seek(&p,0));assert(subtitle_disabled==before+1&&!p.active_subtitle_track);
 }
 puts("Production player seek adapter: HW/SW landed position, subtitle choice, Off and failed seeks passed.");
}
