#include "media/audio_volume.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SCE_AUDIO_VOLUME_0DB 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 2
static int port_percent;
int sceAudioOutSetVolume(int port,int flags,const int *volume){
 assert(port==1&&flags==3&&volume[0]==volume[1]);port_percent=volume[0];return 0;
}
#include "background-gain.inc"
int main(void){
 assert(vt_audio_volume_percent()==100);
 const int requests[]={INT_MIN,-1,0,50,100,125,150,151,200,250,300,301,INT_MAX};
 const int expected[]={0,0,0,50,100,125,150,151,200,250,300,300,300};
 for(unsigned i=0;i<sizeof(requests)/sizeof(*requests);i++){
  vt_audio_volume_set_percent(requests[i]);assert(vt_audio_volume_percent()==expected[i]);
  assert(vt_audio_volume_toggle_mute()&&vt_audio_volume_percent()==0);
  assert(vt_audio_volume_level_percent()==expected[i]);
  assert(!vt_audio_volume_toggle_mute()&&vt_audio_volume_percent()==expected[i]);
  int applied=-1;set_port_volume(1,requests[i],&applied);
  assert(port_percent==vita_audio_hardware_percent(expected[i])*32768/100);
 }
 int16_t *original=malloc(65536*sizeof(int16_t)),*samples=malloc(65536*sizeof(int16_t));assert(original&&samples);
 for(int i=0;i<65536;i++)original[i]=(int16_t)(i-32768);
 for(int percent=0;percent<=300;percent++){
  amplify_pcm16(original,samples,65536*sizeof(int16_t),percent);
  for(int i=0;i<65536;i++){
   double value=original[i]*(percent>100?percent/100.0:1.0);
   int expected_sample=value>32767?32767:value<-32768?-32768:(int)value;
   /* Floating reference may differ by one at an exact integer boundary. */
   assert(abs(samples[i]-expected_sample)<=1);
  }
 }
 int16_t wave[4800],boosted[4800];double energy=0;
 for(unsigned i=0;i<4800;i++){wave[i]=(int16_t)(5000*sin(2*3.141592653589793*440*i/48000));energy+=(double)wave[i]*wave[i];}
 for(int percent=150;percent<=300;percent+=50){
  double boost_energy=0;vita_audio_boost_pcm16(wave,boosted,4800,percent);
  for(unsigned i=0;i<4800;i++)boost_energy+=(double)boosted[i]*boosted[i];
  double ratio=sqrt(boost_energy/energy);assert(fabs(ratio-percent/100.0)<.0002);
  printf("Actual PCM RMS: %d%% = %.5fx\n",percent,ratio);
 }
 memcpy(boosted,wave,sizeof(wave));vita_audio_boost_pcm16(boosted,boosted,4800,300);
 for(unsigned i=0;i<4800;i++)assert(abs(boosted[i])<=15000);
 free(original);free(samples);
 puts("Volume: 19.7M signed-sample checks, 0..300 bounds, exact normal PCM, clipping, port unity, mute restoration and 1.5/2/2.5/3x RMS gain passed.");
}
