/* Real FFmpeg decoder/resampler, independent of Vita video/system boundaries. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libavformat/avformat.h>
#include <vita_sw_audio_decoder.h>
#define open_audio vita_sw_audio_decoder_open
#define send_audio vita_sw_audio_decoder_send
#define get_audio vita_sw_audio_decoder_receive
#define close_audio vita_sw_audio_decoder_close
static uint64_t frames;static long double energy[2];static int64_t first,last;static unsigned outputs;
static int collect(VitaSwAudioDecoder *decoder){
 VitaDecodedAudio pcm;int64_t pts;int r;
 while((r=get_audio(decoder,&pcm,&pts))==1){
  assert(pcm.channels==2&&pcm.sample_rate==48000&&pcm.frames<=VITA_SOFTWARE_AUDIO_FRAMES);
  assert(pts!=AV_NOPTS_VALUE);if(!outputs)first=pts;else assert(pts>=last&&pts-last<180000);
  last=pts;outputs++;frames+=pcm.frames;
  for(unsigned i=0;i<pcm.frames;i++)for(unsigned ch=0;ch<2;ch++)energy[ch]+=(long double)pcm.pcm[i*2+ch]*pcm.pcm[i*2+ch];
 }
 return r;
}
int main(int argc,char **argv){
 assert(argc==2);av_log_set_level(AV_LOG_ERROR);AVFormatContext *f=NULL;assert(!avformat_open_input(&f,argv[1],NULL,NULL));assert(avformat_find_stream_info(f,NULL)>=0);
 VitaSwAudioDecoder *decoder=vita_sw_audio_decoder_create();assert(decoder);
 for(unsigned index=0;index<f->nb_streams;index++){
  AVStream *stream=f->streams[index];enum AVCodecID id=stream->codecpar->codec_id;if(id!=AV_CODEC_ID_AAC&&id!=AV_CODEC_ID_MP3&&id!=AV_CODEC_ID_AC3)continue;
  assert(!open_audio(decoder,stream->codecpar,stream->time_base));
  assert(av_seek_frame(f,-1,0,AVSEEK_FLAG_BACKWARD)>=0);frames=outputs=0;energy[0]=energy[1]=0;first=last=0;
  AVPacket *packet=av_packet_alloc();assert(packet);
  while(av_read_frame(f,packet)>=0){if(packet->stream_index==(int)index){int r=send_audio(decoder,packet);assert(!r);r=collect(decoder);assert(!r);}av_packet_unref(packet);}
  assert(!send_audio(decoder,NULL));assert(collect(decoder)==AVERROR_EOF);assert(frames>280000&&frames<301000&&outputs>10);
  assert(sqrt((double)(energy[0]/frames))>500&&sqrt((double)(energy[1]/frames))>500);
  assert(first>=-150000&&first<50000&&last>5800000&&last<6300000);
  printf("Software audio %s: %llu stereo PCM frames, initial=%lld us, final=%lld us, RMS %.1f/%.1f; complete decoder/resampler drain.\n",avcodec_get_name(id),(unsigned long long)frames,(long long)first,(long long)last,sqrt((double)(energy[0]/frames)),sqrt((double)(energy[1]/frames)));
  close_audio(decoder);close_audio(decoder);av_packet_free(&packet);
 }
 vita_sw_audio_decoder_destroy(decoder);avformat_close_input(&f);return 0;
}
