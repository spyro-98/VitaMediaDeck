#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "media/audio_gain.h"
#include <vita_audio_playback.h>
typedef unsigned SceSize;
typedef struct {
 VitaAudioPlayback *signals;
 volatile int producer_done;
 uint32_t pcm_read_seq,pcm_write_seq,sample_rate,output_grain;
 uint32_t pcm_end_ms[8];uint8_t pcm_pts_valid[8];
 int16_t *pcm_ring;
 unsigned channels;
 int port;
} AudioPlaybackEngine;
#define SCE_AUDIO_VOLUME_0DB 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 2
static unsigned submitted,drains;
static int hardware;
static AudioPlaybackEngine *current;
static const int levels[]={100,150,200,250,300,0,75,100};
static const int16_t input[]={10000,-10000,30000,-30000,32767,-32768,0,123};
static int sceKernelGetThreadCurrentPriority(void){return 64;}
static int sceKernelDelayThread(unsigned us){(void)us;assert(!"Unexpected wait for a prefilled ring");return 0;}
static uint64_t sceKernelGetProcessTimeWide(void){return 100000+submitted*22000;}
static void log_printf(const char *format,...){(void)format;}
static int sceAudioOutSetVolume(int port,int flags,const int *v){assert(port==1&&flags==3&&v[0]==v[1]);hardware=v[0];return 0;}
static int sceAudioOutOutput(int port,const void *data);
#include "output.inc"
#define run_output audio_output_thread
#define set_volume(engine,percent) playback_set_volume((engine)->signals,(percent))
static int sceAudioOutOutput(int port,const void *data){
 if(!data){assert(port==1);drains++;return 0;}
 assert(port==1&&submitted<8);
 int percent=vita_audio_volume_clamp(levels[submitted]);
 assert(hardware==vita_audio_hardware_percent(percent)*32768/100);
 const int16_t *pcm=data;
 for(unsigned i=0;i<current->output_grain*current->channels;i++){
  int value=input[i%8]*(percent>100?percent:100)/100;
  if(value>32767)value=32767;if(value<-32768)value=-32768;
  assert(pcm[i]==value);
 }
 /* Original queued data must not retain any prior boost or clipping. */
 for(unsigned grain=0;grain<8;grain++)for(unsigned i=0;i<current->output_grain*current->channels;i++)
  assert(current->pcm_ring[grain*3072+i]==input[i%8]);
 if(++submitted<8)set_volume(current,levels[submitted]);
 return 0;
}
int main(void){
 static int16_t ring[8*3072];volatile int cancel=0;
 for(unsigned output_grain=576;output_grain<=1536;output_grain+=output_grain==576?448:output_grain==1024?128:384)for(unsigned channels=1;channels<=2;channels++){
  VitaAudioPlayback signals={.cancel=&cancel};
  AudioPlaybackEngine st={.signals=&signals,.pcm_write_seq=8,.producer_done=1,.sample_rate=48000,.output_grain=output_grain,.port=1,.pcm_ring=ring};
  st.channels=channels;
  for(unsigned grain=0;grain<8;grain++)for(unsigned i=0;i<output_grain*channels;i++)ring[grain*3072+i]=input[i%8];
  current=&st;submitted=drains=0;set_volume(&st,100);AudioPlaybackEngine *self=&st;
  assert(!run_output(sizeof(self),&self));assert(submitted==8&&drains==1&&st.pcm_read_seq==8&&!signals.had_error);
  set_volume(&st,-1);assert(signals.volume_percent==0);set_volume(&st,300);assert(signals.volume_percent==300);
 }
 puts("Actual decoder AudioOut consumer: mono/stereo queued raw PCM, immediate 100/150/200/250/300/mute/75/100 changes, actual 300 cap, no double boost or stale clipping passed.");
}
