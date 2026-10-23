#include "network/dash_source.h"
#include "network/dash_manifest.h"
#include "common/text_log.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/parseutils.h>
#include <libavutil/time.h>

#define INIT_LIMIT (512u*1024u)
#define IO_BUFFER 32768
typedef struct DashSource DashSource;
typedef struct {
    DashSource *owner;
    unsigned char *initialization,*media;
    size_t init_size,media_size,offset;
    AVFormatContext *demux;
    AVIOContext *io;
    AVPacket *packet;
    VtDashSegment segment;
    uint64_t next_tick;
    int started,ended,pending;
    int64_t dts_us,pts_us,last_dts_us;
    unsigned packets;
} DashTrack;
struct DashSource {
    VtIptvSourceConfig config;
    char url[VT_IPTV_URL_MAX];
    VtDashManifest *manifest;
    DashTrack tracks[2];
    volatile int *cancel;
    AVFormatContext *mux;
    AVIOContext *io;
    unsigned char *output;
    size_t output_size,output_capacity;
    int output_error,finished,clock_set;
    int64_t origin_us,clock_offset_us;
    uint64_t reload_at,segments,packets;
    uint32_t audio_sample_rate,audio_channels;
};
static int interrupted(DashSource *s) {return s->cancel&&__atomic_load_n(s->cancel,__ATOMIC_ACQUIRE);}
static int interrupt_ffmpeg(void *ctx) {return interrupted(ctx);}
static int download(DashSource *s,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective) {
    for(int attempt=0;attempt<3;attempt++) {
        *data=NULL;*size=0;effective[0]=0;
        if(interrupted(s))return VT_IPTV_CANCELLED;
        int64_t started=av_gettime_relative();
        int r=s->config.fetch(s->config.fetch_ctx,url,limit,data,size,effective,VT_IPTV_URL_MAX,s->cancel);
        int64_t elapsed=av_gettime_relative()-started;
        if(elapsed>=500000)log_printf("DASH slow fetch: bytes=%u elapsed=%lld ms result=%d attempt=%d\n",
            (unsigned)*size,(long long)(elapsed/1000),r,attempt+1);
        if(interrupted(s)){free(*data);*data=NULL;return VT_IPTV_CANCELLED;}
        if(!r) {
            char verified[VT_IPTV_URL_MAX];
            if(!*data||!*size||*size>limit)r=VT_IPTV_LIMIT;
            else r=vt_iptv_resolve_url(url,effective,verified,sizeof(verified));
        }
        if(!r)return 0;
        free(*data);*data=NULL;
        if((r!=VT_IPTV_IO&&r!=VT_IPTV_DNS&&r!=VT_IPTV_TIMEOUT)||attempt==2)return r;
    }
    return VT_IPTV_IO;
}
static int manifest_load(DashSource *s,uint64_t now) {
    unsigned char *body=NULL;size_t size=0;char effective[VT_IPTV_URL_MAX];
    int r=download(s,s->url,VT_IPTV_PLAYLIST_LIMIT,&body,&size,effective);
    if(r)return r;
    VtDashManifest *fresh=NULL;int64_t utc=av_gettime()+s->clock_offset_us;
    r=vt_dash_manifest_parse(body,size,effective,utc,&s->config,&fresh);
    if(!r&&fresh->dynamic&&!s->clock_set&&fresh->utc_value[0]) {
        char clock_text[256];clock_text[0]=0;
        if(fresh->utc_http) {
            unsigned char *clock_body=NULL;size_t clock_size=0;char target[VT_IPTV_URL_MAX];
            int cr=download(s,fresh->utc_value,255,&clock_body,&clock_size,target);
            if(!cr&&!memchr(clock_body,0,clock_size)){memcpy(clock_text,clock_body,clock_size);clock_text[clock_size]=0;}
            free(clock_body);
            if(cr==VT_IPTV_CANCELLED)r=cr;
        } else if(strlen(fresh->utc_value)<sizeof(clock_text))memcpy(clock_text,fresh->utc_value,strlen(fresh->utc_value)+1);
        size_t n=strlen(clock_text);while(n&&(clock_text[n-1]=='\r'||clock_text[n-1]=='\n'||clock_text[n-1]==' '))clock_text[--n]=0;
        int64_t server;
        if(!r&&clock_text[0]&&av_parse_time(&server,clock_text,0)>=0) {
            s->clock_offset_us=server-av_gettime();vt_dash_manifest_free(fresh);fresh=NULL;
            r=vt_dash_manifest_parse(body,size,effective,server,&s->config,&fresh);
        }
        s->clock_set=1;
    }
    free(body);
    if(!r&&s->manifest&&!vt_dash_same_tracks(s->manifest,fresh))r=VT_IPTV_DISCONTINUITY;
    if(r){vt_dash_manifest_free(fresh);return r;}
    int initial=!s->manifest;
    vt_dash_manifest_free(s->manifest);s->manifest=fresh;
    s->reload_at=fresh->dynamic?now+fresh->update_ms:UINT64_MAX;
    if(initial)log_printf("DASH manifest: live=%d video=%ux%u bitrate=%u audio=%u duration=%llu ms\n",
        fresh->dynamic,fresh->tracks[0].width,fresh->tracks[0].height,fresh->tracks[0].bandwidth,
        fresh->tracks[1].bandwidth,(unsigned long long)(fresh->duration_us/1000));
    return 0;
}
static int deny_io(AVFormatContext *fmt,AVIOContext **io,const char *url,int flags,AVDictionary **options) {
    (void)fmt;(void)io;(void)url;(void)flags;(void)options;return AVERROR(EPERM);
}
static int memory_read(void *opaque,uint8_t *out,int capacity) {
    DashTrack *t=opaque;if(interrupted(t->owner))return AVERROR_EXIT;
    size_t total=t->init_size+t->media_size;
    if(t->offset>=total)return AVERROR_EOF;
    size_t n=(size_t)capacity;if(n>total-t->offset)n=total-t->offset;
    size_t copied=0;
    if(t->offset<t->init_size) {
        copied=t->init_size-t->offset;if(copied>n)copied=n;
        memcpy(out,t->initialization+t->offset,copied);
    }
    if(n>copied)memcpy(out+copied,t->media+(t->offset+copied-t->init_size),n-copied);
    t->offset+=n;return (int)n;
}
static int64_t memory_seek(void *opaque,int64_t offset,int whence) {
    DashTrack *t=opaque;int64_t total=(int64_t)t->init_size+(int64_t)t->media_size;
    if(whence==AVSEEK_SIZE)return total;
    whence&=~AVSEEK_FORCE;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?(int64_t)t->offset:whence==SEEK_END?total:-1;
    if(base<0||(offset>0&&offset>INT64_MAX-base)||(offset<0&&offset< -base))return AVERROR(EINVAL);
    offset+=base;if(offset<0||offset>total)return AVERROR(EINVAL);t->offset=(size_t)offset;return offset;
}
static void fragment_close(DashTrack *t) {
    avformat_close_input(&t->demux);
    if(t->io){av_freep(&t->io->buffer);avio_context_free(&t->io);}
    free(t->media);t->media=NULL;t->media_size=0;
}
static int fragment_open(DashSource *s,int index) {
    DashTrack *t=&s->tracks[index];const VtDashTrack *track=&s->manifest->tracks[index];
    VtDashSegment segment;int r;
    if(!t->started) {
        int64_t target=s->manifest->period_us;
        if(s->manifest->dynamic) {
            int64_t end=s->manifest->tracks[0].end_us<s->manifest->tracks[1].end_us?s->manifest->tracks[0].end_us:s->manifest->tracks[1].end_us;
            int64_t duration=s->manifest->tracks[0].segment_us>s->manifest->tracks[1].segment_us?s->manifest->tracks[0].segment_us:s->manifest->tracks[1].segment_us;
            int64_t delay=(int64_t)s->manifest->delay_ms*1000;if(delay<duration*3)delay=duration*3;
            /* Leave room behind the live edge for prefill plus three fragments
             * of delivery jitter. Zero retains the legacy caller policy. */
            if(s->config.startup_segments) {
                int64_t reserve=(int64_t)s->config.startup_segments*1000000+duration*3;
                if(delay<reserve)delay=reserve;
            }
            target=end-delay;
            int64_t first=s->manifest->tracks[0].first_us>s->manifest->tracks[1].first_us?s->manifest->tracks[0].first_us:s->manifest->tracks[1].first_us;
            if(target<first)target=first;
            if(index==1&&s->tracks[0].started)target=s->tracks[0].segment.start_us;
        }
        r=vt_dash_start(s->manifest,index,target,&segment);
    } else r=vt_dash_segment(s->manifest,index,t->next_tick,&segment);
    if(r)return r;
    if(t->started&&segment.time!=t->next_tick)return VT_IPTV_DISCONTINUITY;
    if(!t->initialization) {
        char effective[VT_IPTV_URL_MAX];
        r=download(s,track->initialization,INIT_LIMIT,&t->initialization,&t->init_size,effective);if(r)return r;
    }
    fragment_close(t);char effective[VT_IPTV_URL_MAX];
    r=download(s,segment.url,s->config.segment_limit,&t->media,&t->media_size,effective);if(r)return r;
    t->offset=0;t->packets=0;
    unsigned char *buffer=av_malloc(IO_BUFFER);if(!buffer)return VT_IPTV_MEMORY;
    t->io=avio_alloc_context(buffer,IO_BUFFER,0,t,memory_read,NULL,memory_seek);
    if(!t->io){av_free(buffer);return VT_IPTV_MEMORY;}
    t->demux=avformat_alloc_context();if(!t->demux)return VT_IPTV_MEMORY;
    t->demux->pb=t->io;t->demux->flags|=AVFMT_FLAG_CUSTOM_IO;
    t->demux->io_open=deny_io;t->demux->max_streams=2;
    t->demux->interrupt_callback=(AVIOInterruptCB){interrupt_ffmpeg,s};
    AVDictionary *options=NULL;av_dict_set(&options,"enable_drefs","0",0);av_dict_set(&options,"use_absolute_path","0",0);
    r=avformat_open_input(&t->demux,NULL,av_find_input_format("mov"),&options);av_dict_free(&options);
    if(r<0||t->demux->nb_streams!=1)return VT_IPTV_UNSUPPORTED;
    if(t->demux->streams[0]->time_base.num<=0||t->demux->streams[0]->time_base.den<=0)return VT_IPTV_INVALID;
    AVCodecParameters *codec=t->demux->streams[0]->codecpar;
    if(codec->codec_id!=(index?AV_CODEC_ID_AAC:AV_CODEC_ID_H264))return VT_IPTV_UNSUPPORTED;
    if(index) {
        int object_type=codec->extradata_size>=2?codec->extradata[0]>>3:0;
        if(codec->ch_layout.nb_channels<1||codec->ch_layout.nb_channels>2||codec->sample_rate<=0||codec->sample_rate>48000||
           (object_type!=2&&object_type!=5&&object_type!=29))return VT_IPTV_UNSUPPORTED;
        s->audio_sample_rate=codec->sample_rate;s->audio_channels=codec->ch_layout.nb_channels;
    } else if(codec->width<=0||codec->height<=0||codec->width>(int)s->config.width_limit||codec->height>(int)s->config.height_limit)return VT_IPTV_UNSUPPORTED;
    t->segment=segment;t->next_tick=segment.time+segment.duration;t->started=1;s->segments++;
    return 0;
}
static int packet_next(DashSource *s,int index) {
    DashTrack *t=&s->tracks[index];if(t->pending)return 0;if(t->ended)return VT_IPTV_END;
    for(int boundary=0;boundary<3;boundary++) {
        if(interrupted(s))return VT_IPTV_CANCELLED;
        if(!t->demux) {
            int r=fragment_open(s,index);
            if(r==VT_IPTV_END)t->ended=1;
            if(r)return r;
        }
        int r=av_read_frame(t->demux,t->packet);
        if(r==AVERROR_EOF) {
            if(!t->packets)return VT_IPTV_INVALID;
            fragment_close(t);continue;
        }
        if(r<0)return interrupted(s)?VT_IPTV_CANCELLED:VT_IPTV_INVALID;
        if(++t->packets>100000||t->packet->size<0||(size_t)t->packet->size>s->config.segment_limit)return VT_IPTV_LIMIT;
        if(t->packet->stream_index!=0||t->packet->dts==AV_NOPTS_VALUE||t->packet->pts==AV_NOPTS_VALUE)return VT_IPTV_UNSUPPORTED;
        size_t side_size=0;
        if(av_packet_get_side_data(t->packet,AV_PKT_DATA_ENCRYPTION_INFO,&side_size))return VT_IPTV_UNSUPPORTED;
        const VtDashTrack *track=&s->manifest->tracks[index];
        int64_t offset=av_rescale(track->offset,1000000,track->timescale);
        int64_t dts=av_rescale_q(t->packet->dts,t->demux->streams[0]->time_base,AV_TIME_BASE_Q);
        int64_t pts=av_rescale_q(t->packet->pts,t->demux->streams[0]->time_base,AV_TIME_BASE_Q);
        if(dts==INT64_MIN||pts==INT64_MIN||offset==INT64_MIN||dts<INT64_MIN+offset||pts<INT64_MIN+offset)return VT_IPTV_INVALID;
        dts-=offset;pts-=offset;
        if(dts>INT64_MAX-s->manifest->period_us||pts>INT64_MAX-s->manifest->period_us)return VT_IPTV_INVALID;
        t->dts_us=dts+s->manifest->period_us;t->pts_us=pts+s->manifest->period_us;
        if(t->last_dts_us!=INT64_MIN&&t->dts_us<=t->last_dts_us)return VT_IPTV_DISCONTINUITY;
        /* Reject forged/truncated fragments whose clock does not belong to
         * the advertised segment; allow B-frame preroll and AAC priming. */
        if(t->pts_us<t->segment.start_us-2000000||t->pts_us>t->segment.end_us+2000000)return VT_IPTV_DISCONTINUITY;
        if(index==1&&s->manifest->dynamic&&t->last_dts_us==INT64_MIN&&s->tracks[0].pending) {
            int64_t length=av_rescale_q(t->packet->duration,t->demux->streams[0]->time_base,AV_TIME_BASE_Q);
            if(length>0&&length<=1000000&&t->pts_us+length<=s->tracks[0].pts_us) {
                av_packet_unref(t->packet);boundary--;continue;
            }
        }
        t->pending=1;return 0;
    }
    return VT_IPTV_INVALID;
}
static int mux_write(void *opaque,uint8_t *bytes,int count) {
    DashSource *s=opaque;
    if(interrupted(s))return AVERROR_EXIT;
    size_t n=(size_t)count;
    if(count<0||n>s->config.segment_limit-s->output_size){s->output_error=VT_IPTV_LIMIT;return AVERROR(ENOSPC);}
    if(n>s->output_capacity-s->output_size) {
        size_t capacity=s->output_capacity?s->output_capacity:65536;
        while(capacity<s->output_size+n)capacity*=2;
        if(capacity>s->config.segment_limit)capacity=s->config.segment_limit;
        void *p=realloc(s->output,capacity);if(!p){s->output_error=VT_IPTV_MEMORY;return AVERROR(ENOMEM);}
        s->output=p;s->output_capacity=capacity;
    }
    memcpy(s->output+s->output_size,bytes,n);s->output_size+=n;return count;
}
static int mux_open(DashSource *s) {
    int r=avformat_alloc_output_context2(&s->mux,NULL,"mpegts",NULL);if(r<0||!s->mux)return VT_IPTV_MEMORY;
    uint8_t *buffer=av_malloc(IO_BUFFER);if(!buffer)return VT_IPTV_MEMORY;
    s->io=avio_alloc_context(buffer,IO_BUFFER,1,s,NULL,mux_write,NULL);
    if(!s->io){av_free(buffer);return VT_IPTV_MEMORY;}
    s->mux->pb=s->io;s->mux->flags|=AVFMT_FLAG_CUSTOM_IO;
    s->mux->max_delay=0;s->mux->avoid_negative_ts=AVFMT_AVOID_NEG_TS_DISABLED;
    for(int i=0;i<2;i++) {
        AVStream *stream=avformat_new_stream(s->mux,NULL);if(!stream)return VT_IPTV_MEMORY;
        r=avcodec_parameters_copy(stream->codecpar,s->tracks[i].demux->streams[0]->codecpar);if(r<0)return VT_IPTV_MEMORY;
        stream->codecpar->codec_tag=0;stream->time_base=(AVRational){1,90000};
    }
    int64_t first=s->tracks[0].dts_us<s->tracks[1].dts_us?s->tracks[0].dts_us:s->tracks[1].dts_us;
    if(first<INT64_MIN+1000000)return VT_IPTV_INVALID;
    s->origin_us=first-1000000;
    AVDictionary *options=NULL;
    av_dict_set(&options,"mpegts_copyts","1",0);av_dict_set(&options,"mpegts_flags","resend_headers",0);
    av_dict_set(&options,"pat_period","0.1",0);av_dict_set(&options,"pcr_period","20",0);
    r=avformat_write_header(s->mux,&options);av_dict_free(&options);
    if(r<0)return VT_IPTV_UNSUPPORTED;
    log_printf("DASH remux: H.264/AAC packet copy; shared origin=%lld us audio delta=%lld us\n",
        (long long)s->origin_us,(long long)(s->tracks[1].pts_us-s->tracks[0].pts_us));
    return 0;
}
static void *source_open(const char *url,const VtIptvSourceConfig *config) {
    DashSource *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->config=*config;snprintf(s->url,sizeof(s->url),"%s",url);
    for(int i=0;i<2;i++) {
        s->tracks[i].owner=s;s->tracks[i].last_dts_us=INT64_MIN;
        s->tracks[i].packet=av_packet_alloc();
        if(!s->tracks[i].packet){for(int j=0;j<=i;j++)av_packet_free(&s->tracks[j].packet);free(s);return NULL;}
    }
    return s;
}
static int source_next(void *opaque,uint64_t now,volatile int *cancel,unsigned char **data,size_t *size,int *finite,uint64_t *duration) {
    DashSource *s=opaque;s->cancel=cancel;*data=NULL;*size=0;
    int r=0;
    if(!s->manifest||(!s->finished&&now>=s->reload_at))r=manifest_load(s,now);
    if(s->manifest){*finite=!s->manifest->dynamic;*duration=s->manifest->duration_us/1000;}
    if(r)goto failed;
    if(s->finished)return VT_IPTV_END;
    int64_t chunk_start=INT64_MIN;
    for(unsigned n=0;n<10000;n++) {
        int ready[2];
        for(int i=0;i<2;i++) {
            ready[i]=packet_next(s,i);
            if(ready[i]!=0&&ready[i]!=VT_IPTV_END&&ready[i]!=VT_IPTV_WAIT){r=ready[i];goto failed;}
        }
        if(ready[0]==VT_IPTV_WAIT||ready[1]==VT_IPTV_WAIT)break;
        if(ready[0]==VT_IPTV_END&&ready[1]==VT_IPTV_END) {
            if(s->mux&&av_write_trailer(s->mux)<0){r=VT_IPTV_INVALID;goto failed;}
            s->finished=1;break;
        }
        if(!s->mux) {
            if(ready[0]||ready[1]){r=VT_IPTV_UNSUPPORTED;goto failed;}
            r=mux_open(s);if(r)goto failed;
        }
        int index=ready[0]==VT_IPTV_END?1:ready[1]==VT_IPTV_END?0:s->tracks[0].dts_us<=s->tracks[1].dts_us?0:1;
        DashTrack *t=&s->tracks[index];
        if(chunk_start==INT64_MIN)chunk_start=t->dts_us;
        if(t->dts_us<s->origin_us||t->pts_us<s->origin_us){r=VT_IPTV_DISCONTINUITY;goto failed;}
        AVRational base=s->mux->streams[index]->time_base;
        t->packet->dts=av_rescale_q(t->dts_us-s->origin_us,AV_TIME_BASE_Q,base);
        t->packet->pts=av_rescale_q(t->pts_us-s->origin_us,AV_TIME_BASE_Q,base);
        t->packet->duration=av_rescale_q(t->packet->duration,t->demux->streams[0]->time_base,base);
        t->packet->stream_index=index;t->packet->pos=-1;
        int wrote=av_write_frame(s->mux,t->packet);
        av_packet_unref(t->packet);t->pending=0;t->last_dts_us=t->dts_us;s->packets++;
        if(wrote<0){r=s->output_error?s->output_error:VT_IPTV_INVALID;goto failed;}
        if(t->dts_us-chunk_start>=1000000)break;
    }
    if(s->io)avio_flush(s->io);
    if(s->output_error){r=s->output_error;goto failed;}
    if(!s->output_size)return s->finished?VT_IPTV_END:VT_IPTV_WAIT;
    unsigned char *exact=realloc(s->output,s->output_size);if(!exact){r=VT_IPTV_MEMORY;goto failed;}
    *data=exact;*size=s->output_size;s->output=NULL;s->output_size=s->output_capacity=0;
    return VT_IPTV_OK;
failed:
    log_printf("DASH source stopped: result=%d segments=%llu packets=%llu\n",r,
        (unsigned long long)s->segments,(unsigned long long)s->packets);
    return r;
}
static void source_close(void *opaque) {
    DashSource *s=opaque;if(!s)return;
    for(int i=0;i<2;i++) {
        fragment_close(&s->tracks[i]);free(s->tracks[i].initialization);av_packet_free(&s->tracks[i].packet);
    }
    avformat_free_context(s->mux);
    if(s->io){av_freep(&s->io->buffer);avio_context_free(&s->io);}
    vt_dash_manifest_free(s->manifest);free(s->output);free(s);
}
static void audio_info(void *opaque,uint32_t *rate,uint32_t *channels) {
    DashSource *s=opaque;*rate=s->audio_sample_rate;*channels=s->audio_channels;
}
void vt_dash_source_configure(VtIptvSourceConfig *config) {
    if(config)config->producer=(VtIptvProducer){source_open,source_next,source_close,audio_info};
}
