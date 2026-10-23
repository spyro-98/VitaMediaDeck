/* Host FFmpeg benchmark: compare output and work for 1/2/3 frame threads.
 * Apple Silicon timings are not Vita timings or energy measurements. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/crc.h>
#include <libavutil/error.h>
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static double cpu(void){struct rusage r;getrusage(RUSAGE_SELF,&r);return r.ru_utime.tv_sec+r.ru_utime.tv_usec/1e6+r.ru_stime.tv_sec+r.ru_stime.tv_usec/1e6;}
typedef struct {unsigned frames;uint32_t crc;} Result;
static void drain(AVCodecContext *ctx,AVFrame *f,Result *result){
 int ret;const AVCRC *table=av_crc_get_table(AV_CRC_32_IEEE);
 while((ret=avcodec_receive_frame(ctx,f))>=0){
  assert(f->format==AV_PIX_FMT_YUV420P);result->frames++;
  for(int p=0;p<3;p++)for(int y=0;y<(p?(f->height+1)/2:f->height);y++)
   result->crc=av_crc(table,result->crc,f->data[p]+(ptrdiff_t)y*f->linesize[p],p?(f->width+1)/2:f->width);
  av_frame_unref(f);
 }
 assert(ret==AVERROR(EAGAIN)||ret==AVERROR_EOF);
}
int main(int argc,char **argv){
 assert(argc==2);Result expected={0};av_log_set_level(AV_LOG_ERROR);
 puts("threads,frames,crc32,wall_ms,cpu_ms,host_only");
 for(int threads=1;threads<=3;threads++)for(int repeat=0;repeat<3;repeat++){
  AVFormatContext *fmt=NULL;assert(avformat_open_input(&fmt,argv[1],NULL,NULL)==0);assert(avformat_find_stream_info(fmt,NULL)>=0);
  int stream=av_find_best_stream(fmt,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);assert(stream>=0);
  AVCodecContext *ctx=avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));assert(ctx);
  assert(avcodec_parameters_to_context(ctx,fmt->streams[stream]->codecpar)>=0);
  ctx->thread_count=threads;ctx->thread_type=threads==1?0:FF_THREAD_FRAME;ctx->flags2|=AV_CODEC_FLAG2_FAST;
  assert(avcodec_open2(ctx,avcodec_find_decoder(AV_CODEC_ID_H264),NULL)==0);
  AVFrame *frame=av_frame_alloc();AVPacket *pkt=av_packet_alloc();Result got={0};
  uint64_t start=now();double start_cpu=cpu();
  while(av_read_frame(fmt,pkt)>=0){
   if(pkt->stream_index==stream){assert(avcodec_send_packet(ctx,pkt)==0);drain(ctx,frame,&got);}
   av_packet_unref(pkt);
  }
  assert(avcodec_send_packet(ctx,NULL)==0);drain(ctx,frame,&got);
  double work=cpu()-start_cpu;uint64_t elapsed=now()-start;
  if(!expected.frames)expected=got;assert(got.frames==expected.frames&&got.crc==expected.crc);
  printf("%d,%u,%08x,%.3f,%.3f,true\n",threads,got.frames,got.crc,elapsed/1000.0,work*1000);
  av_packet_free(&pkt);av_frame_free(&frame);avcodec_free_context(&ctx);avformat_close_input(&fmt);
 }
}
