#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/error.h>
#ifdef TEST_SW
#include "../../../vita-sw-decoder/src/internal/input_timing.h"
#else
#include "../../../vita-hw-decoder/src/internal/input_timing.h"
#endif
static unsigned checks;
#define CHECK(x) do { checks++;assert(x); } while(0)
static int read_bytes(void*p,uint8_t*b,int n){int r=fread(b,1,n,p);return r?r:AVERROR_EOF;}
static int64_t seek_bytes(void*p,int64_t n,int w){if((w&~AVSEEK_FORCE)==AVSEEK_SIZE){int64_t pos=ftello(p);fseeko(p,0,SEEK_END);int64_t size=ftello(p);fseeko(p,pos,SEEK_SET);return size;}return fseeko(p,n,w&~AVSEEK_FORCE)?-1:ftello(p);}
static void file_test(const char*path){
 FILE*file=fopen(path,"rb");CHECK(file);
 AVIOContext*io=avio_alloc_context(av_malloc(65536),65536,0,file,read_bytes,NULL,seek_bytes);CHECK(io);io->seekable=AVIO_SEEKABLE_NORMAL;
 AVFormatContext*f=avformat_alloc_context();CHECK(f);f->pb=io;f->flags|=AVFMT_FLAG_CUSTOM_IO|AVFMT_FLAG_FAST_SEEK;
 CHECK(avformat_open_input(&f,NULL,NULL,NULL)>=0);
 uint64_t duration=decoder_input_duration_ms(f);
 CHECK(duration>=23900&&duration<=25000);
 int video=av_find_best_stream(f,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);CHECK(video>=0);
 const int targets[]={18000,4000,22000,1000,12000};
 AVPacket*p=av_packet_alloc();CHECK(p);
 for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);i++){
  int64_t target=av_rescale_q(targets[i],(AVRational){1,1000},f->streams[video]->time_base);
  CHECK(av_seek_frame(f,video,target,AVSEEK_FLAG_BACKWARD)>=0);
  int found=0;
  for(int n=0;n<3000&&av_read_frame(f,p)>=0;n++){
   if(p->stream_index==video){int64_t pts=p->pts!=AV_NOPTS_VALUE?p->pts:p->dts;
    int64_t ms=av_rescale_q(pts,f->streams[video]->time_base,(AVRational){1,1000});
    CHECK(ms>=targets[i]-4000&&ms<=targets[i]+1000);found=1;av_packet_unref(p);break;}
   av_packet_unref(p);
  }
  CHECK(found);
 }
 av_packet_free(&p);
 printf("Duration + bidirectional indexed seek: %s => %llu ms (aggregate header=%lld)\n",path,(unsigned long long)duration,(long long)f->duration);
 avformat_close_input(&f);av_freep(&io->buffer);avio_context_free(&io);fclose(file);
}
static void metadata_test(void){
 AVFormatContext*f=avformat_alloc_context();CHECK(f);f->duration=AV_NOPTS_VALUE;f->start_time=AV_NOPTS_VALUE;
 CHECK(!decoder_input_duration_ms(NULL));CHECK(!decoder_input_duration_ms(f));
 AVStream*v=avformat_new_stream(f,NULL);CHECK(v);v->codecpar->codec_type=AVMEDIA_TYPE_VIDEO;v->time_base=(AVRational){1,1000};v->start_time=5000;v->duration=10000;
 CHECK(decoder_input_duration_ms(f)==10000);
 AVStream*a=avformat_new_stream(f,NULL);CHECK(a);a->codecpar->codec_type=AVMEDIA_TYPE_AUDIO;a->time_base=(AVRational){1,1000};a->start_time=0;a->duration=12000;
 CHECK(decoder_input_duration_ms(f)==15000);
 v->start_time=-1000;CHECK(decoder_input_duration_ms(f)==13000);
 v->disposition=AV_DISPOSITION_ATTACHED_PIC;v->duration=INT64_MAX;CHECK(decoder_input_duration_ms(f)==12000);
 AVStream*sub=avformat_new_stream(f,NULL);CHECK(sub);sub->codecpar->codec_type=AVMEDIA_TYPE_SUBTITLE;sub->time_base=(AVRational){1,1000};sub->start_time=-1000;sub->duration=1000000;
 CHECK(decoder_input_duration_ms(f)==13000); /* Same origin as the player, no subtitle-only tail. */
 f->duration=17000123;CHECK(decoder_input_duration_ms(f)==17000);
 f->duration=AV_NOPTS_VALUE;a->duration=AV_NOPTS_VALUE;CHECK(!decoder_input_duration_ms(f));
 a->duration=INT64_MAX;a->start_time=INT64_MAX;CHECK(!decoder_input_duration_ms(f));
 avformat_free_context(f);
}
int main(int argc,char**argv){metadata_test();for(int i=1;i<argc;i++)file_test(argv[i]);printf("Duration/seek production helper: %u checks passed.\n",checks);}
