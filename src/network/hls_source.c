#include "network/hls_source.h"
#include "network/iptv_trace.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavutil/aes.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>

#define STARTUP_PACKETS 128
#define STARTUP_BYTES (2u*1024u*1024u)
#define IO_SIZE 32768
#define RESOURCE_MEMORY (24u*1024u*1024u)
typedef struct HlsSource HlsSource;
typedef struct {
    HlsSource *source;
    unsigned char *data;
    size_t size, offset;
    AVIOContext *io;
} Resource;
struct HlsSource {
    VtIptvSourceConfig config;
    VtIptvSelection active;
    char url[VT_IPTV_RESOURCE_URL_MAX];
    volatile int *cancel;
    AVFormatContext *demux, *mux;
    AVIOContext *root_io, *output_io;
    AVPacket *packet;
    int map[16], finished, error, output_error, origin_set;
    int64_t origin, video_decode_lead, video_lead_limit, last_dts[16];
    unsigned timestamp_probe_frames, startup_count, startup_pos;
    AVPacket *startup[STARTUP_PACKETS];
    uint32_t audio_rate, audio_channels;
    char embedded_languages[16][64];
    int ts_inspected;
    unsigned char *output;
    size_t output_size, output_capacity, resource_bytes;
    unsigned resources;
    uint64_t requests, packets, encrypted;
};
static int interrupted(void *opaque) {
    HlsSource *s=opaque;
    return s->cancel && __atomic_load_n(s->cancel,__ATOMIC_ACQUIRE);
}
static int read_resource(void *opaque,uint8_t *data,int capacity) {
    Resource *r=opaque;
    if(interrupted(r->source))return AVERROR_EXIT;
    if(r->source->error)return AVERROR(EIO);
    if(r->offset>=r->size)return AVERROR_EOF;
    size_t n=r->size-r->offset;if(n>(size_t)capacity)n=(size_t)capacity;
    memcpy(data,r->data+r->offset,n);r->offset+=n;return (int)n;
}
static int64_t seek_resource(void *opaque,int64_t offset,int whence) {
    Resource *r=opaque;
    if(whence==AVSEEK_SIZE)return (int64_t)r->size;
    whence&=~AVSEEK_FORCE;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?(int64_t)r->offset:whence==SEEK_END?(int64_t)r->size:-1;
    if(base<0 || offset< -base || offset>INT64_MAX-base)return AVERROR(EINVAL);
    offset+=base;if(offset>(int64_t)r->size)return AVERROR(EINVAL);
    r->offset=(size_t)offset;return offset;
}
static int close_resource(AVFormatContext *format,AVIOContext *io) {
    (void)format;if(!io)return 0;
    Resource *r=io->opaque;
    r->source->resource_bytes-=r->size;r->source->resources--;
    free(r->data);free(r);av_freep(&io->buffer);avio_context_free(&io);return 0;
}
static int hex_bytes(const char *text,unsigned char out[16]) {
    if(!text || strlen(text)!=32)return VT_IPTV_INVALID;
    for(unsigned i=0;i<16;i++) {
        unsigned value=0;
        for(unsigned j=0;j<2;j++) {
            unsigned char c=(unsigned char)text[i*2+j];int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if(digit<0)return VT_IPTV_INVALID;
            value=value*16+(unsigned)digit;
        }
        out[i]=(unsigned char)value;
    }
    return 0;
}
static int decrypt_segment(unsigned char *data,size_t *size,AVDictionary *options) {
    const AVDictionaryEntry *key=av_dict_get(options,"key",NULL,0),*iv=av_dict_get(options,"iv",NULL,0);
    unsigned char key_bytes[16],iv_bytes[16];
    if(!*size || *size%16 || *size/16>INT_MAX || !key || !iv ||
        hex_bytes(key->value,key_bytes) || hex_bytes(iv->value,iv_bytes))return VT_IPTV_INVALID;
    struct AVAES *aes=av_aes_alloc();if(!aes)return VT_IPTV_MEMORY;
    int result=av_aes_init(aes,key_bytes,128,1);
    memset(key_bytes,0,sizeof(key_bytes));
    if(result<0){av_free(aes);return VT_IPTV_INVALID;}
    av_aes_crypt(aes,data,data,(int)(*size/16),iv_bytes,1);av_free(aes);
    unsigned padding=data[*size-1];if(!padding || padding>16 || padding>*size)return VT_IPTV_INVALID;
    for(unsigned i=0;i<padding;i++)if(data[*size-1-i]!=padding)return VT_IPTV_INVALID;
    *size-=padding;return *size?0:VT_IPTV_INVALID;
}
static int download(HlsSource *s,const char *url,int64_t offset,int64_t end,
    unsigned char **data,size_t *size,char effective[VT_IPTV_RESOURCE_URL_MAX]) {
    size_t limit=s->config.segment_limit;
    if(end>=0 && (offset<0 || end<=offset || (uint64_t)(end-offset)>limit))return VT_IPTV_LIMIT;
    for(int attempt=0;attempt<3;attempt++) {
        *data=NULL;*size=0;effective[0]=0;
        if(interrupted(s))return VT_IPTV_CANCELLED;
        int result;
        if(end>=0 && s->config.fetch_range)
            result=s->config.fetch_range(s->config.fetch_ctx,url,(uint64_t)offset,(size_t)(end-offset),
                data,size,effective,VT_IPTV_RESOURCE_URL_MAX,s->cancel);
        else result=s->config.fetch(s->config.fetch_ctx,url,limit,data,size,effective,VT_IPTV_RESOURCE_URL_MAX,s->cancel);
        s->requests++;
        if(interrupted(s))result=VT_IPTV_CANCELLED;
        char checked[VT_IPTV_RESOURCE_URL_MAX];
        if(!result && (!*data || !*size || *size>limit))result=VT_IPTV_LIMIT;
        if(!result)result=vt_iptv_resource_resolve(url,effective,checked,sizeof(checked));
        if(!result && end>=0) {
            if(s->config.fetch_range) {if(*size!=(size_t)(end-offset))result=VT_IPTV_INVALID;}
            else if((uint64_t)end>*size)result=VT_IPTV_INVALID;
            else {memmove(*data,*data+(size_t)offset,(size_t)(end-offset));*size=(size_t)(end-offset);}
        } else if(!result && offset>0)result=VT_IPTV_INVALID;
        if(!result)return 0;
        free(*data);*data=NULL;*size=0;
        if((result!=VT_IPTV_IO && result!=VT_IPTV_DNS && result!=VT_IPTV_TIMEOUT) || attempt==2)return result;
    }
    return VT_IPTV_IO;
}
static int option_number(AVDictionary *options,const char *key,int64_t fallback,int64_t *number) {
    const AVDictionaryEntry *entry=av_dict_get(options,key,NULL,0);*number=fallback;
    if(!entry)return 0;
    char *end;errno=0;long long n=strtoll(entry->value,&end,10);
    if(errno || *end || n<0)return VT_IPTV_INVALID;
    *number=n;return 0;
}
static int deny_metadata_io(AVFormatContext *fmt,AVIOContext **io,const char *url,int flags,AVDictionary **options){
    (void)fmt;(void)io;(void)url;(void)flags;(void)options;return AVERROR(EPERM);
}
/* FFmpeg 6 HLS copies codec parameters but drops per-stream MPEG-TS language
 * tags. Read just the first local segment's bounded PMT/PES prefix, without
 * another fetch, decoder or nested I/O. Stream order within this single muxed
 * playlist is preserved by HLS. External renditions already have their labels. */
static void inspect_ts_languages(HlsSource *s,unsigned char *data,size_t size){
    if(s->ts_inspected||!s->config.stream_options||
       size<376||data[0]!=0x47||data[188]!=0x47)return;
    s->ts_inspected=1;
    Resource memory={.source=s,.data=data,.size=size>256u*1024u?256u*1024u:size};
    unsigned char *buffer=av_malloc(IO_SIZE);if(!buffer)return;
    AVIOContext *io=avio_alloc_context(buffer,IO_SIZE,0,&memory,read_resource,NULL,seek_resource);
    if(!io){av_free(buffer);return;}
    AVFormatContext *fmt=avformat_alloc_context();
    if(fmt){
        fmt->pb=io;fmt->flags|=AVFMT_FLAG_CUSTOM_IO;fmt->io_open=deny_metadata_io;
        fmt->probesize=256u*1024u;fmt->max_analyze_duration=0;fmt->max_streams=16;
        fmt->interrupt_callback=(AVIOInterruptCB){interrupted,s};
        if(avformat_open_input(&fmt,NULL,av_find_input_format("mpegts"),NULL)>=0){
            AVPacket *packet=av_packet_alloc();
            if(packet){for(int i=0;i<4;i++){int r=av_read_frame(fmt,packet);av_packet_unref(packet);if(r<0)break;}av_packet_free(&packet);}
            if(fmt->nb_programs==1&&!s->config.stream_options->channel_name[0]){
                AVDictionaryEntry *name=av_dict_get(fmt->programs[0]->metadata,"service_name",NULL,0);
                if(name)snprintf(s->config.stream_options->channel_name,sizeof(s->config.stream_options->channel_name),"%s",name->value);
            }
            unsigned audio=0;
            for(unsigned i=0;i<fmt->nb_streams&&audio<16;i++)if(fmt->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO){
                AVDictionaryEntry *v=av_dict_get(fmt->streams[i]->metadata,"language",NULL,0);
                if(v)snprintf(s->embedded_languages[audio],sizeof(s->embedded_languages[audio]),"%s",v->value);
                audio++;
            }
        }
        avformat_close_input(&fmt);
    }
    av_freep(&io->buffer);avio_context_free(&io);
}
static int open_resource(AVFormatContext *format,AVIOContext **io,const char *url,int flags,AVDictionary **options) {
    HlsSource *s=format->opaque;int result=0;*io=NULL;
    /* FFmpeg normally skips a failed segment. After our bounded retries are
     * exhausted, do not clear the failure by opening the next segment and
     * silently introduce a hole into the shared A/V timeline. */
    if(s->error)return AVERROR(EIO);
    if(flags!=AVIO_FLAG_READ || s->resources>=12){s->error=VT_IPTV_LIMIT;return AVERROR(EPERM);}
    int crypto=!strncmp(url,"crypto+",7);const char *address=crypto?url+7:url;
    if(!strncmp(address,"file:",5)&&vt_iptv_local_path(s->url)&&vt_iptv_local_path(address+5))address+=5;
    char checked[VT_IPTV_RESOURCE_URL_MAX],effective[VT_IPTV_RESOURCE_URL_MAX];
    result=vt_iptv_resource_resolve(address,address,checked,sizeof(checked));
    int64_t offset=0,end=-1;
    if(!result)result=option_number(options?*options:NULL,"offset",0,&offset);
    if(!result)result=option_number(options?*options:NULL,"end_offset",-1,&end);
    unsigned char *data=NULL;size_t size=0;
    if(!result)result=download(s,address,offset,end,&data,&size,effective);
    if(!result && crypto) {result=decrypt_segment(data,&size,options?*options:NULL);if(result==VT_IPTV_INVALID)result=VT_IPTV_ENCRYPTED;if(!result)s->encrypted++;}
    const unsigned char *text=data;size_t text_size=size;
    if(text_size>=3 && !memcmp(text,"\xef\xbb\xbf",3)){text+=3;text_size-=3;}
    if(!result && text_size>=7 && !memcmp(text,"#EXTM3U",7)) {
        unsigned char *prepared=NULL;size_t prepared_size=0;
        result=vt_hls_prepare_manifest(data,size,effective,&s->config,&prepared,&prepared_size);
        if(result==VT_IPTV_INVALID)result=VT_IPTV_MANIFEST;
        free(data);data=prepared;size=prepared_size;
    }
    if(!result)inspect_ts_languages(s,data,size);
    if(!result && size>RESOURCE_MEMORY-s->resource_bytes)result=VT_IPTV_LIMIT;
    if(!result) {
        unsigned char *exact=realloc(data,size);
        if(!exact)result=VT_IPTV_MEMORY;else data=exact;
    }
    Resource *resource=NULL;unsigned char *buffer=NULL;
    if(!result) {
        resource=calloc(1,sizeof(*resource));buffer=av_malloc(IO_SIZE);
        if(!resource || !buffer)result=VT_IPTV_MEMORY;
    }
    if(!result) {
        resource->source=s;resource->data=data;resource->size=size;
        resource->io=avio_alloc_context(buffer,IO_SIZE,0,resource,read_resource,NULL,seek_resource);
        if(!resource->io)result=VT_IPTV_MEMORY;
    }
    if(result) {
        free(resource);av_free(buffer);free(data);s->error=result;
        IPTV_TRACE("HLS resource failed: result=%d ranged=%d encrypted=%d requests=%llu\n",result,end>=0,crypto,(unsigned long long)s->requests);
        return result==VT_IPTV_CANCELLED?AVERROR_EXIT:AVERROR(EIO);
    }
    s->resources++;s->resource_bytes+=size;*io=resource->io;s->error=0;return 0;
}
static int write_output(void *opaque,uint8_t *data,int size) {
    HlsSource *s=opaque;
    if(interrupted(s))return AVERROR_EXIT;
    if(size<0 || (size_t)size>s->config.segment_limit-s->output_size) {
        s->output_error=VT_IPTV_LIMIT;return AVERROR(ENOSPC);
    }
    size_t need=s->output_size+(size_t)size;
    if(need>s->output_capacity) {
        size_t capacity=s->output_capacity?s->output_capacity*2:65536;
        if(capacity<need)capacity=need;
        if(capacity>s->config.segment_limit)capacity=s->config.segment_limit;
        void *p=realloc(s->output,capacity);if(!p){s->output_error=VT_IPTV_MEMORY;return AVERROR(ENOMEM);}
        s->output=p;s->output_capacity=capacity;
    }
    memcpy(s->output+s->output_size,data,(size_t)size);s->output_size=need;return size;
}
static int initialize(HlsSource *s) {
    const AVInputFormat *hls=av_find_input_format("hls");if(!hls)return VT_IPTV_UNSUPPORTED;
    s->demux=avformat_alloc_context();if(!s->demux)return VT_IPTV_MEMORY;
    s->demux->opaque=s;s->demux->io_open=open_resource;s->demux->io_close2=close_resource;
    s->demux->interrupt_callback=(AVIOInterruptCB){interrupted,s};
    s->demux->flags|=AVFMT_FLAG_CUSTOM_IO;s->demux->probesize=1024*1024;s->demux->max_analyze_duration=2000000;
    s->demux->max_streams=16;
    int r=open_resource(s->demux,&s->root_io,s->url,AVIO_FLAG_READ,NULL);if(r<0)return s->error;
    s->demux->pb=s->root_io;
    AVDictionary *options=NULL;
    /* Memory AVIOs are not HTTP URLContexts. Never let HLS reuse them as
     * persistent sockets or open parallel internal FFmpeg connections. */
    av_dict_set(&options,"http_persistent","0",0);av_dict_set(&options,"http_multiple","0",0);
    av_dict_set(&options,"http_seekable","0",0);av_dict_set(&options,"live_start_index","-4",0);
    av_dict_set(&options,"max_reload","6",0);av_dict_set(&options,"m3u8_hold_counters","6",0);
    av_dict_set(&options,"allowed_extensions","ALL",0);
    char demux_url[VT_IPTV_RESOURCE_URL_MAX+6];
    snprintf(demux_url,sizeof(demux_url),"%s%s",vt_iptv_local_path(s->url)?"file:":"",s->url);
    r=avformat_open_input(&s->demux,demux_url,hls,&options);av_dict_free(&options);
    if(s->error)return s->error;
    if(r<0)return s->error?s->error:VT_IPTV_MANIFEST;
    r=avformat_find_stream_info(s->demux,NULL);
    if(s->error)return s->error;
    if(r<0)return s->error?s->error:VT_IPTV_MANIFEST;
    if(s->demux->nb_streams>16)return VT_IPTV_LIMIT;
    int embedded_choice=-1;
    if(!strncmp(s->config.selection.audio,"embedded:",9))embedded_choice=atoi(s->config.selection.audio+9);
    VtIptvOptions *available=s->config.stream_options;
    int inspect_audio=available&&(!available->audio_count||!strncmp(available->audio[0].id,"embedded:",9));
    if(inspect_audio)available->audio_count=0;
    int audio_ordinal=0;
    int video=-1,audio=-1,saw_video=0,saw_h264=0,saw_audio=0,saw_aac=0;
    for(unsigned i=0;i<s->demux->nb_streams;i++) {
        AVStream *stream=s->demux->streams[i];AVCodecParameters *par=stream->codecpar;
        stream->discard=AVDISCARD_ALL;
        if(par->codec_type==AVMEDIA_TYPE_VIDEO){saw_video=1;saw_h264|=par->codec_id==AV_CODEC_ID_H264;}
        if(par->codec_type==AVMEDIA_TYPE_AUDIO){saw_audio=1;saw_aac|=par->codec_id==AV_CODEC_ID_AAC;}
        if(video<0 && par->codec_type==AVMEDIA_TYPE_VIDEO && par->codec_id==AV_CODEC_ID_H264 &&
           par->width>0 && par->height>0 && (!s->config.width_limit || (unsigned)par->width<=s->config.width_limit) && (!s->config.height_limit || (unsigned)par->height<=s->config.height_limit))video=(int)i;
        if(inspect_audio&&par->codec_type==AVMEDIA_TYPE_AUDIO&&available->audio_count<VT_IPTV_CHOICE_MAX){
            VtIptvChoice *c=&available->audio[available->audio_count++];memset(c,0,sizeof(*c));
            snprintf(c->id,sizeof(c->id),"embedded:%u",i);
            AVDictionaryEntry *v=av_dict_get(stream->metadata,"language",NULL,0);
            if(v)snprintf(c->language,sizeof(c->language),"%s",v->value);
            else if(audio_ordinal<16)snprintf(c->language,sizeof(c->language),"%s",s->embedded_languages[audio_ordinal]);
            v=av_dict_get(stream->metadata,"title",NULL,0);if(v)snprintf(c->name,sizeof(c->name),"%s",v->value);
            c->supported=par->codec_id==AV_CODEC_ID_AAC&&par->sample_rate>0&&par->sample_rate<=48000&&par->ch_layout.nb_channels>0&&par->ch_layout.nb_channels<=2;
        }
        if(audio<0 && (embedded_choice<0||(unsigned)embedded_choice==i) && par->codec_type==AVMEDIA_TYPE_AUDIO && par->codec_id==AV_CODEC_ID_AAC &&
           par->sample_rate>0 && par->sample_rate<=48000 && par->ch_layout.nb_channels>0 && par->ch_layout.nb_channels<=2)audio=(int)i;
        if(par->codec_type==AVMEDIA_TYPE_AUDIO)audio_ordinal++;
        IPTV_TRACE("HLS track: index=%u type=%d codec=%d size=%dx%d audio=%d/%d\n",i,par->codec_type,par->codec_id,par->width,par->height,par->sample_rate,par->ch_layout.nb_channels);
    }
    if(video<0)return saw_video&&saw_h264?VT_IPTV_VIDEO_SIZE:VT_IPTV_VIDEO_CODEC;
    if(audio<0)return saw_audio&&saw_aac?VT_IPTV_AUDIO_FORMAT:VT_IPTV_AUDIO_CODEC;
    /* MPEG-TS already has valid decode timestamps. Preserve them: adding a
     * fictitious 17-frame PTS/DTS gap changes the live timeline, even for
     * streams with no B-frames. Only length-prefixed CMAF may need a decode
     * lead for signed composition offsets, bounded by its actual reordering. */
    AVCodecParameters *video_parameters=s->demux->streams[video]->codecpar;
    int avcc=video_parameters->extradata_size>=7 && video_parameters->extradata &&
        video_parameters->extradata[0]==1;
    int reorder=video_parameters->video_delay;
    if(reorder<0 || reorder>16)return VT_IPTV_UNSUPPORTED;
    AVRational fps=s->demux->streams[video]->avg_frame_rate;
    if(fps.num<=0 || fps.den<=0)fps=s->demux->streams[video]->r_frame_rate;
    if(fps.num<=0 || fps.den<=0)fps=(AVRational){25,1};
    s->video_decode_lead=avcc?av_rescale_q(reorder,av_inv_q(fps),AV_TIME_BASE_Q):0;
    if(s->video_decode_lead>2000000)return VT_IPTV_UNSUPPORTED;
    /* Some reordered TS sources have negative composition offsets, including
     * explicit DTS and PTS-only PES with inferred DTS. Observe a bounded startup
     * window before publishing bytes, then use one constant decode lead.
     * PTS and the audio timeline are never shifted. */
    if(!avcc && reorder>0) {
        s->timestamp_probe_frames=(unsigned)reorder+3;
        s->video_lead_limit=av_rescale_q(reorder+2,av_inv_q(fps),AV_TIME_BASE_Q);
        if(s->video_lead_limit>2000000)s->video_lead_limit=2000000;
    }
    IPTV_TRACE("HLS timeline: avcc=%d reorder=%d decode-lead=%lld us\n",avcc,reorder,(long long)s->video_decode_lead);
    if(avformat_alloc_output_context2(&s->mux,NULL,"mpegts",NULL)<0 || !s->mux)return VT_IPTV_MEMORY;
    unsigned char *buffer=av_malloc(IO_SIZE);if(!buffer)return VT_IPTV_MEMORY;
    s->output_io=avio_alloc_context(buffer,IO_SIZE,1,s,NULL,write_output,NULL);
    if(!s->output_io){av_free(buffer);return VT_IPTV_MEMORY;}
    s->mux->pb=s->output_io;s->mux->flags|=AVFMT_FLAG_CUSTOM_IO;
    s->mux->max_delay=0;s->mux->avoid_negative_ts=AVFMT_AVOID_NEG_TS_DISABLED;
    for(int i=0;i<2;i++) {
        int index=i?audio:video;AVStream *in=s->demux->streams[index],*out=avformat_new_stream(s->mux,NULL);
        if(!out || avcodec_parameters_copy(out->codecpar,in->codecpar)<0)return VT_IPTV_MEMORY;
        out->codecpar->codec_tag=0;out->time_base=(AVRational){1,90000};s->map[index]=i;in->discard=AVDISCARD_DEFAULT;
    }
    /* Status is read under the source mutex, but producer callbacks aren't.
     * Publish only immutable scalar audio configuration atomically. */
    __atomic_store_n(&s->audio_rate,(uint32_t)s->demux->streams[audio]->codecpar->sample_rate,__ATOMIC_RELEASE);
    __atomic_store_n(&s->audio_channels,(uint32_t)s->demux->streams[audio]->codecpar->ch_layout.nb_channels,__ATOMIC_RELEASE);
    av_dict_set(&options,"mpegts_copyts","1",0);av_dict_set(&options,"mpegts_flags","resend_headers",0);
    av_dict_set(&options,"pat_period","0.1",0);av_dict_set(&options,"pcr_period","20",0);
    r=avformat_write_header(s->mux,&options);av_dict_free(&options);
    if(r<0)return VT_IPTV_UNSUPPORTED;
    if(s->config.stream_options){
        VtIptvOptions *o=s->config.stream_options;
        o->active=s->active;
        if(!o->active.audio[0])snprintf(o->active.audio,sizeof(o->active.audio),"embedded:%d",audio);
        o->active_width=s->demux->streams[video]->codecpar->width;
        o->active_height=s->demux->streams[video]->codecpar->height;
        o->active_bandwidth=0;
        for(unsigned i=0;i<o->video_count;i++)if(!strcmp(o->video[i].id,o->active.video))o->active_bandwidth=o->video[i].bandwidth;
    }
    s->config.active_selection=NULL;
    IPTV_TRACE("HLS remux ready: video=%d audio=%d separate renditions joined; requests=%llu AES segments=%llu\n",video,audio,(unsigned long long)s->requests,(unsigned long long)s->encrypted);
    return 0;
}
static void *source_open(const char *url,const VtIptvSourceConfig *config) {
    HlsSource *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->config=*config;s->config.active_selection=&s->active;snprintf(s->url,sizeof(s->url),"%s",url);
    for(int i=0;i<16;i++){s->map[i]=-1;s->last_dts[i]=INT64_MIN;}
    s->packet=av_packet_alloc();if(!s->packet){free(s);return NULL;}return s;
}
/* Packet references only, transiently bounded at 128 packets / 2 MiB.
 * This runs once on the producer; no per-frame allocation after replay. */
static int prepare_decode_order(HlsSource *s) {
    if(!s->timestamp_probe_frames)return 0;
    unsigned video_frames=0;size_t bytes=0;int64_t previous=INT64_MIN,required=0;
    while(video_frames<s->timestamp_probe_frames) {
        if(interrupted(s))return VT_IPTV_CANCELLED;
        int r=av_read_frame(s->demux,s->packet);
        if(s->error)return s->error;
        if(r==AVERROR_EOF)break;
        if(r<0)return VT_IPTV_MEDIA_DATA;
        int index=s->packet->stream_index;
        if(index<0 || index>=16)return VT_IPTV_MEDIA_DATA;
        if(s->map[index]<0){av_packet_unref(s->packet);continue;}
        if(s->startup_count==STARTUP_PACKETS || s->packet->size<0 ||
           (size_t)s->packet->size>STARTUP_BYTES-bytes)return VT_IPTV_LIMIT;
        if(s->packet->pts==AV_NOPTS_VALUE || s->packet->dts==AV_NOPTS_VALUE)return VT_IPTV_TIMESTAMPS;
        AVStream *in=s->demux->streams[index];
        if(in->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {
            int64_t dts=av_rescale_q(s->packet->dts,in->time_base,AV_TIME_BASE_Q);
            int64_t pts=av_rescale_q(s->packet->pts,in->time_base,AV_TIME_BASE_Q);
            if(dts<INT64_MIN+5000000 || dts>INT64_MAX-5000000 || pts<INT64_MIN+5000000 || pts>INT64_MAX-5000000)return VT_IPTV_TIMESTAMPS;
            if(previous!=INT64_MIN && dts<=previous && previous-dts<=100000)dts=previous+12;
            previous=dts;
            if(dts>pts) {
                if(pts<dts-s->video_lead_limit)return VT_IPTV_TIMESTAMPS;
                if(dts-pts>required)required=dts-pts;
            }
            video_frames++;
        }
        AVPacket *saved=av_packet_alloc();if(!saved)return VT_IPTV_MEMORY;
        bytes+=(size_t)s->packet->size;av_packet_move_ref(saved,s->packet);
        s->startup[s->startup_count++]=saved;
    }
    if(required>s->video_lead_limit)return VT_IPTV_TIMESTAMPS;
    if(required>s->video_decode_lead)s->video_decode_lead=required+12; /* One 90 kHz tick of rounding headroom. */
    IPTV_TRACE("HLS decode order: observed=%lld lead=%lld us packets=%u video=%u\n",
        (long long)required,(long long)s->video_decode_lead,s->startup_count,video_frames);
    s->timestamp_probe_frames=0;return 0;
}
static int read_packet(HlsSource *s) {
    if(s->startup_pos<s->startup_count) {
        AVPacket **saved=&s->startup[s->startup_pos++];
        av_packet_move_ref(s->packet,*saved);av_packet_free(saved);return 0;
    }
    return av_read_frame(s->demux,s->packet);
}
static int source_next(void *opaque,uint64_t now,volatile int *cancel,unsigned char **data,size_t *size,int *finite,uint64_t *duration) {
    (void)now;HlsSource *s=opaque;s->cancel=cancel;*data=NULL;*size=0;
    int result=0;
    if(!s->mux){result=initialize(s);if(result)goto failed;}
    *finite=s->demux->duration!=AV_NOPTS_VALUE && s->demux->duration>0;
    *duration=*finite?(uint64_t)(s->demux->duration/1000):0;
    if(s->finished)return VT_IPTV_END;
    result=prepare_decode_order(s);if(result)goto failed;
    int64_t start=INT64_MIN;
    for(unsigned n=0;n<10000;n++) {
        if(interrupted(s)){result=VT_IPTV_CANCELLED;goto failed;}
        int r=read_packet(s);
        if(s->error){result=s->error;goto failed;}
        if(r==AVERROR_EOF) {
            if(s->error){result=s->error;goto failed;}
            if(!*finite){result=VT_IPTV_IO;goto failed;}
            if(av_write_trailer(s->mux)<0){result=VT_IPTV_REMUX;goto failed;}
            s->finished=1;break;
        }
        if(r<0){result=s->error?s->error:VT_IPTV_MEDIA_DATA;goto failed;}
        int index=s->packet->stream_index;
        if(index<0 || index>=16){result=VT_IPTV_INVALID;goto failed;}
        if(s->map[index]<0){av_packet_unref(s->packet);continue;}
        if(av_packet_get_side_data(s->packet,AV_PKT_DATA_ENCRYPTION_INFO,NULL)){result=VT_IPTV_ENCRYPTED;goto failed;}
        if(s->packet->pts==AV_NOPTS_VALUE || s->packet->dts==AV_NOPTS_VALUE){result=VT_IPTV_TIMESTAMPS;goto failed;}
        AVStream *in=s->demux->streams[index],*out=s->mux->streams[s->map[index]];
        int64_t dts=av_rescale_q(s->packet->dts,in->time_base,AV_TIME_BASE_Q);
        int64_t pts=av_rescale_q(s->packet->pts,in->time_base,AV_TIME_BASE_Q);
        if(dts<INT64_MIN+5000000 || pts<INT64_MIN+5000000 || dts>INT64_MAX-5000000 || pts>INT64_MAX-5000000){result=VT_IPTV_INVALID;goto failed;}
        if(in->codecpar->codec_type==AVMEDIA_TYPE_VIDEO)dts-=s->video_decode_lead;
        if(!s->origin_set){s->origin=dts-3000000;s->origin_set=1;}
        /* Some live encoders repeat/round video decode timestamps around
         * field/reorder boundaries. Preserve compressed decode order and PTS;
         * break a small DTS tie by one 90 kHz tick, not by shifting A/V time.
         * Larger epoch changes remain explicit session discontinuities. */
        if(in->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && s->last_dts[index]!=INT64_MIN &&
           dts<=s->last_dts[index] && s->last_dts[index]-dts<=100000)
            dts=s->last_dts[index]+12;
        if((s->last_dts[index]!=INT64_MIN && (dts<=s->last_dts[index] || dts-s->last_dts[index]>10000000)) || dts<s->origin || pts<s->origin) {
            IPTV_TRACE("HLS timestamp reset: track=%d dts=%lld previous=%lld pts=%lld origin=%lld\n",index,(long long)dts,(long long)s->last_dts[index],(long long)pts,(long long)s->origin);
            /* Never silently give the two readers different clock epochs. */
            result=VT_IPTV_DISCONTINUITY;goto failed;
        }
        if(dts>pts){result=VT_IPTV_TIMESTAMPS;goto failed;}
        s->last_dts[index]=dts;
        if(start==INT64_MIN)start=dts;
        s->packet->dts=av_rescale_q(dts-s->origin,AV_TIME_BASE_Q,out->time_base);
        s->packet->pts=av_rescale_q(pts-s->origin,AV_TIME_BASE_Q,out->time_base);
        s->packet->duration=av_rescale_q(s->packet->duration,in->time_base,out->time_base);
        s->packet->stream_index=out->index;s->packet->pos=-1;
        r=av_write_frame(s->mux,s->packet);av_packet_unref(s->packet);s->packets++;
        if(r<0){result=s->output_error?s->output_error:VT_IPTV_REMUX;goto failed;}
        if(dts-start>=1000000)break;
    }
    avio_flush(s->output_io);
    if(s->output_error){result=s->output_error;goto failed;}
    if(!s->output_size)return s->finished?VT_IPTV_END:VT_IPTV_WAIT;
    unsigned char *exact=realloc(s->output,s->output_size);
    if(!exact){result=VT_IPTV_MEMORY;goto failed;}
    *data=exact;*size=s->output_size;s->output=NULL;s->output_size=s->output_capacity=0;return 0;
failed:
    if(interrupted(s))result=VT_IPTV_CANCELLED;
    IPTV_TRACE("HLS remux stopped: result=%d requests=%llu packets=%llu AES=%llu\n",result,(unsigned long long)s->requests,(unsigned long long)s->packets,(unsigned long long)s->encrypted);
    return result;
}
static void source_close(void *opaque) {
    HlsSource *s=opaque;if(!s)return;
    avformat_close_input(&s->demux);
    if(s->root_io)close_resource(NULL,s->root_io);
    avformat_free_context(s->mux);
    if(s->output_io){av_freep(&s->output_io->buffer);avio_context_free(&s->output_io);}
    for(unsigned i=0;i<s->startup_count;i++)av_packet_free(&s->startup[i]);
    av_packet_free(&s->packet);free(s->output);free(s);
}
static void audio_info(void *opaque,uint32_t *rate,uint32_t *channels) {
    HlsSource *s=opaque;*rate=__atomic_load_n(&s->audio_rate,__ATOMIC_ACQUIRE);*channels=__atomic_load_n(&s->audio_channels,__ATOMIC_ACQUIRE);
}
void vt_hls_source_configure(VtIptvSourceConfig *config) {
    if(config)config->producer=(VtIptvProducer){source_open,source_next,source_close,audio_info};
}
