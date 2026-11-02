#include "media/audio_formats.h"
#include <assert.h>
#include <stdio.h>
#include <libavformat/avformat.h>
static void shape(AVCodecParameters *p,enum AVCodecID codec,int channels,int rate){p->codec_type=AVMEDIA_TYPE_AUDIO;p->codec_id=codec;p->sample_rate=rate;av_channel_layout_uninit(&p->ch_layout);av_channel_layout_default(&p->ch_layout,channels);}
int main(void){
 AVCodecParameters *p=avcodec_parameters_alloc();assert(p);
 for(int ch=1;ch<=2;ch++){
  shape(p,AV_CODEC_ID_AAC,ch,48000);assert(vmd_audio_parameters_playable(p));
  shape(p,AV_CODEC_ID_MP3,ch,11025);assert(vmd_audio_parameters_playable(p));
 }
 shape(p,AV_CODEC_ID_AC3,6,48000);assert(vmd_audio_parameters_playable(p));
 shape(p,AV_CODEC_ID_AC3,7,48000);assert(!vmd_audio_parameters_playable(p));
 shape(p,AV_CODEC_ID_AAC,6,48000);assert(!vmd_audio_parameters_playable(p));
 shape(p,AV_CODEC_ID_MP3,3,44100);assert(!vmd_audio_parameters_playable(p));
 shape(p,AV_CODEC_ID_MP3,2,96000);assert(!vmd_audio_parameters_playable(p));
 shape(p,AV_CODEC_ID_DTS,2,48000);assert(!vmd_audio_parameters_playable(p));
 shape(p,AV_CODEC_ID_EAC3,2,48000);assert(!vmd_audio_parameters_playable(p));
 p->codec_type=AVMEDIA_TYPE_VIDEO;assert(!vmd_audio_parameters_playable(p));
 assert(avcodec_find_decoder(AV_CODEC_ID_AAC)&&avcodec_find_decoder(AV_CODEC_ID_MP3)&&avcodec_find_decoder(AV_CODEC_ID_AC3));
 avcodec_parameters_free(&p);puts("Audio discovery: aligned app/HW/SW ordinals, mono/stereo AAC/MP3, AC3 5.1 and unsupported codec/rate/layout rejection passed.");return 0;
}
