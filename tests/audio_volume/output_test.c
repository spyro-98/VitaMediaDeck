#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "media/audio_gain.h"
typedef unsigned SceSize;
typedef struct {
 volatile int *start_gate,*cancel;
 volatile int stop,paused,producer_done,had_error,volume_percent;
 uint32_t pcm_read_seq,pcm_write_seq,sample_rate,played_until_ms;
 uint32_t pcm_end_ms[8];uint8_t pcm_pts_valid[8];
 int16_t *pcm_ring;
 struct {unsigned channels;} decoder;
 int port;
} ElementaryAudioState;
#define SCE_AUDIO_VOLUME_0DB 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 2
static unsigned submitted;
static int hardware;
static ElementaryAudioState *current;
static const int levels[]={100,150,100,0,75,300,INT32_MAX};
static const int16_t input[]={10000,-10000,30000,-30000,32767,-32768,0,123};
static int sceKernelGetThreadCurrentPriority(void){return 64;}
static int sceKernelDelayThread(unsigned us){(void)us;assert(!"Unexpected wait for a prefilled ring");return 0;}
static uint64_t sceKernelGetProcessTimeWide(void){return 100000+submitted*22000;}
static void log_printf(const char *format,...){(void)format;}
static int sceAudioOutSetVolume(int port,int flags,const int *v){assert(port==1&&flags==3&&v[0]==v[1]);hardware=v[0];return 0;}
static int sceAudioOutOutput(int port,const void *data);
#ifdef TEST_HW
#include "hw-output.inc"
#define run_output vita_hw_elementary_audio_output_thread
#define set_volume vita_hw_elementary_audio_set_volume
#else
#include "sw-output.inc"
#define run_output vita_sw_elementary_audio_output_thread
#define set_volume vita_sw_elementary_audio_set_volume
#endif
static int sceAudioOutOutput(int port,const void *data){
 assert(port==1&&submitted<7);
 int percent=vita_audio_volume_clamp(levels[submitted]);
 assert(hardware==vita_audio_hardware_percent(percent)*32768/100);
 const int16_t *pcm=data;
 for(unsigned i=0;i<1024*current->decoder.channels;i++){
  int value=input[i%8]*(percent>100?percent:100)/100;
  if(value>32767)value=32767;if(value<-32768)value=-32768;
  assert(pcm[i]==value);
 }
 /* Original queued data must not retain any prior boost or clipping. */
 for(unsigned grain=0;grain<7;grain++)for(unsigned i=0;i<1024*current->decoder.channels;i++)
  assert(current->pcm_ring[grain*2048+i]==input[i%8]);
 if(++submitted<7)set_volume(current,levels[submitted]);
 return 0;
}
int main(void){
 static int16_t ring[8*2048];volatile int cancel=0;
 for(unsigned channels=1;channels<=2;channels++){
  ElementaryAudioState st={.cancel=&cancel,.pcm_write_seq=7,.producer_done=1,.sample_rate=48000,.port=1,.pcm_ring=ring};
  st.decoder.channels=channels;
  for(unsigned grain=0;grain<7;grain++)for(unsigned i=0;i<1024*channels;i++)ring[grain*2048+i]=input[i%8];
  current=&st;submitted=0;set_volume(&st,100);ElementaryAudioState *self=&st;
  assert(!run_output(sizeof(self),&self));assert(submitted==7&&st.pcm_read_seq==7&&!st.had_error);
  set_volume(&st,-1);assert(st.volume_percent==0);set_volume(&st,300);assert(st.volume_percent==150);
 }
 puts("Actual decoder AudioOut consumer: mono/stereo queued raw PCM, immediate 100/150/100/mute/75 changes, legacy 300 cap, no double boost or stale clipping passed.");
}
