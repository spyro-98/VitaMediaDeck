/* Real FFmpeg demux/decode over the production IPTV session adapter.
 * Run with the pinned FFmpeg 6 source libraries and a generated HLS fixture.
 * This validates input/PTS, not Vita HW decoding, audio output or frame pacing. */
#include "network/iptv_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>

static unsigned checks, requests, segments;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static const char *fixture;
int vt_iptv_http_fetch(void *ctx, const char *url, size_t limit, unsigned char **data,
    size_t *size, char *effective, size_t capacity, volatile int *cancel) {
    (void)ctx;
    *data=NULL; *size=0;
    if (__atomic_load_n(cancel,__ATOMIC_ACQUIRE)) return VT_IPTV_CANCELLED;
    const char *name=strrchr(url,'/'); if(!name)return VT_IPTV_INVALID;
    /* Only read files generated inside the supplied fixture directory. */
    if (strstr(name,"..") || strchr(name+1,'/') || strchr(name,'?')) return VT_IPTV_INVALID;
    char path[4096]; snprintf(path,sizeof(path),"%s%s",fixture,name);
    FILE *f=fopen(path,"rb"); if(!f)return VT_IPTV_IO;
    fseek(f,0,SEEK_END);long bytes=ftell(f); rewind(f);
    if(bytes<=0||(unsigned long)bytes>limit){fclose(f);return VT_IPTV_LIMIT;}
    *data=malloc((size_t)bytes);assert(*data);*size=fread(*data,1,(size_t)bytes,f);fclose(f);
    if(*size!=(size_t)bytes)return VT_IPTV_IO;
    snprintf(effective,capacity,"%s",url);
    __atomic_add_fetch(&requests,1,__ATOMIC_RELAXED);
    if(!strstr(url,".m3u8"))__atomic_add_fetch(&segments,1,__ATOMIC_RELAXED);
    return 0;
}
typedef struct { VtDecoderStreamHandle cursor; AVIOContext *io; AVFormatContext *format; } Input;
static int read_bytes(void *ctx,uint8_t *data,int size) {
    VtDecoderStreamHandle *c=ctx;int result=c->read(c->opaque,data,(size_t)size);
    return result>0?result:result==0?AVERROR_EOF:AVERROR(EIO);
}
static void open_input(Input *in,VtDecoderStreamFactory *factory) {
    CHECK(!factory->open(factory->opaque,&in->cursor));
    CHECK(!in->cursor.seek&&in->cursor.size==-1&&in->cursor.abort);
    in->io=avio_alloc_context(av_malloc(65536),65536,0,&in->cursor,read_bytes,NULL,NULL);
    CHECK(in->io);in->io->seekable=0;
    in->format=avformat_alloc_context();CHECK(in->format);
    in->format->pb=in->io;in->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    /* Mirrors the opt-in branch in HW/SW input_open, not the file settings. */
    in->format->skip_estimate_duration_from_pts=1;
    in->format->probesize=512*1024;
    in->format->max_analyze_duration=2*AV_TIME_BASE;
    CHECK(!avformat_open_input(&in->format,NULL,av_find_input_format("mpegts"),NULL));
    CHECK(avformat_find_stream_info(in->format,NULL)>=0);
}
static void close_input(Input *in) {
    avformat_close_input(&in->format);
    av_freep(&in->io->buffer);avio_context_free(&in->io);
    in->cursor.close(in->cursor.opaque);
}
typedef struct { AVCodecContext *codec; AVFrame *frame; int stream; unsigned frames; int64_t first,last,origin; } Decode;
static void decoder_open(Decode *d,Input *in,enum AVMediaType type,int64_t origin) {
    d->stream=av_find_best_stream(in->format,type,-1,-1,NULL,0);CHECK(d->stream>=0);
    AVCodecParameters *p=in->format->streams[d->stream]->codecpar;
    CHECK(p->codec_id==(type==AVMEDIA_TYPE_VIDEO?AV_CODEC_ID_H264:AV_CODEC_ID_AAC));
    const AVCodec *codec=avcodec_find_decoder(p->codec_id);CHECK(codec);
    d->codec=avcodec_alloc_context3(codec);CHECK(d->codec);
    CHECK(avcodec_parameters_to_context(d->codec,p)>=0);
    d->codec->thread_count=1;CHECK(avcodec_open2(d->codec,codec,NULL)>=0);
    d->frame=av_frame_alloc();CHECK(d->frame);d->origin=origin;
    for(unsigned i=0;i<in->format->nb_streams;i++)
        in->format->streams[i]->discard=i==(unsigned)d->stream?AVDISCARD_DEFAULT:AVDISCARD_ALL;
}
static void drain(Decode *d,Input *in) {
    int result;
    while((result=avcodec_receive_frame(d->codec,d->frame))>=0) {
        CHECK(d->frame->best_effort_timestamp!=AV_NOPTS_VALUE);
        int64_t pts=av_rescale_q(d->frame->best_effort_timestamp,
            in->format->streams[d->stream]->time_base,AV_TIME_BASE_Q)-d->origin;
        if(!d->frames)d->first=pts;else CHECK(pts>=d->last);
        d->last=pts;d->frames++;av_frame_unref(d->frame);
    }
    CHECK(result==AVERROR(EAGAIN)||result==AVERROR_EOF);
}
static int decode_step(Decode *d,Input *in) {
    AVPacket *packet=av_packet_alloc();CHECK(packet);
    int result;
    do {av_packet_unref(packet);result=av_read_frame(in->format,packet);}
    while(result>=0&&packet->stream_index!=d->stream);
    if(result>=0){CHECK(avcodec_send_packet(d->codec,packet)>=0);drain(d,in);}
    else {CHECK(result==AVERROR_EOF);CHECK(avcodec_send_packet(d->codec,NULL)>=0);drain(d,in);}
    av_packet_free(&packet);return result>=0;
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"Usage: demux_test <generated HLS fixture directory>\n");return 2;}
    fixture=argv[1];av_log_set_level(AV_LOG_ERROR);
    VtIptvSession *session=vt_iptv_session_create("https://fixture.test/index.m3u8",NULL);CHECK(session);
    VtDecoderStreamFactory factory;vt_iptv_session_factory(session,&factory);
    Input video={0},audio={0};open_input(&video,&factory);open_input(&audio,&factory);
    int64_t origin=video.format->start_time;
    CHECK(origin!=AV_NOPTS_VALUE&&origin>=3600LL*AV_TIME_BASE);
    CHECK(llabs(audio.format->start_time-origin)<1000);
    video.format->start_time=audio.format->start_time=origin;
    video.format->duration=audio.format->duration=AV_NOPTS_VALUE;
    Decode v={0},a={0};decoder_open(&v,&video,AVMEDIA_TYPE_VIDEO,origin);
    decoder_open(&a,&audio,AVMEDIA_TYPE_AUDIO,origin);
    int video_running=1,audio_running=1;
    while(video_running||audio_running) {
        if(video_running)video_running=decode_step(&v,&video);
        if(audio_running)audio_running=decode_step(&a,&audio);
    }
    CHECK(v.frames==360);CHECK(a.frames>=560);CHECK(llabs(v.first-a.first)<50000);
    CHECK(v.last>11900000&&a.last>11900000&&llabs(v.last-a.last)<100000);
    CHECK(!vt_iptv_session_event(session));
    close_input(&audio);close_input(&video);vt_iptv_session_destroy(session);
    CHECK(segments==6); /* Independent demuxers do not fetch duplicate segments. */
    printf("FFmpeg %s: %u assertions; %u video / %u audio frames; shared origin %.6f s; first A/V delta %lld us; %u segments / %u requests\n",
        av_version_info(),checks,v.frames,a.frames,(double)origin/AV_TIME_BASE,
        (long long)(v.first-a.first),segments,requests);
    av_frame_free(&a.frame);av_frame_free(&v.frame);
    avcodec_free_context(&a.codec);avcodec_free_context(&v.codec);return 0;
}
