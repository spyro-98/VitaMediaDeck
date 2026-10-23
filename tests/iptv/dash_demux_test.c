/* Reuse the real two-cursor decode harness with the app-side DASH producer. */
#include <stddef.h>
static void rewrite_fixture(const char *,unsigned char **,size_t *);
#define VMD_FIXTURE_REWRITE
#define main hls_harness_main
#include "demux_test.c"
#undef main
#include "network/dash_source.h"
#include <stdarg.h>
#include <libxml/parser.h>
#include <libavutil/intreadwrite.h>
#include "../../../vita-hw-decoder/include/vita_hw_decoder_live.h"
#include "../../../vita-sw-decoder/include/vita_sw_decoder_live.h"
_Static_assert(sizeof(VitaHwDecoderLiveOptions)==16,"Keep the V1 live ABI size");
_Static_assert(sizeof(VitaSwDecoderLiveOptions)==16,"Keep the V1 live ABI size");
_Static_assert(offsetof(VitaHwDecoderLiveOptions,audio)==offsetof(VitaHwDecoderLiveOptions,reserved),"Keep field offsets");
static int shifted;
static unsigned shifted_fragments;
static void shift_boxes(unsigned char *bytes,size_t size,uint64_t offset) {
    for(size_t p=0;p+8<=size;) {
        uint32_t n=AV_RB32(bytes+p);CHECK(n>=8&&n<=size-p);
        if(!memcmp(bytes+p+4,"moof",4)||!memcmp(bytes+p+4,"traf",4))shift_boxes(bytes+p+8,n-8,offset);
        else if(!memcmp(bytes+p+4,"tfdt",4)) {
            CHECK(n>=20&&bytes[p+8]==1);AV_WB64(bytes+p+12,AV_RB64(bytes+p+12)+offset);shifted_fragments++;
        }
        p+=n;
    }
}
static void shift_templates(xmlNode *node) {
    for(;node;node=node->next) {
        if(!xmlStrcmp(node->name,BAD_CAST"SegmentTemplate")) {
            xmlChar *scale=xmlGetProp(node,BAD_CAST"timescale");CHECK(scale);
            uint64_t offset=strtoull((char*)scale,NULL,10)*3600;xmlFree(scale);
            char number[64];snprintf(number,sizeof(number),"%llu",(unsigned long long)offset);
            xmlSetProp(node,BAD_CAST"presentationTimeOffset",BAD_CAST number);
            for(xmlNode *timeline=node->children;timeline;timeline=timeline->next)
                if(!xmlStrcmp(timeline->name,BAD_CAST"SegmentTimeline"))
                    for(xmlNode *s=timeline->children;s;s=s->next)if(!xmlStrcmp(s->name,BAD_CAST"S")&&xmlHasProp(s,BAD_CAST"t")) {
                        xmlChar *t=xmlGetProp(s,BAD_CAST"t");uint64_t v=strtoull((char*)t,NULL,10);xmlFree(t);
                        snprintf(number,sizeof(number),"%llu",(unsigned long long)(v+offset));xmlSetProp(s,BAD_CAST"t",BAD_CAST number);
                    }
        }
        shift_templates(node->children);
    }
}
static void rewrite_fixture(const char *url,unsigned char **data,size_t *size) {
    if(!shifted)return;
    if(strstr(url,".mpd")) {
        xmlDoc *doc=xmlReadMemory((char*)*data,(int)*size,NULL,NULL,XML_PARSE_NONET);CHECK(doc);
        shift_templates(xmlDocGetRootElement(doc));xmlChar *bytes=NULL;int n=0;xmlDocDumpMemory(doc,&bytes,&n);
        CHECK(n>0);free(*data);*data=malloc(n);CHECK(*data);memcpy(*data,bytes,n);*size=n;xmlFree(bytes);xmlFreeDoc(doc);
    } else if(strstr(url,"chunk-stream"))shift_boxes(*data,*size,(strstr(url,"chunk-stream0-")?15360ULL:48000ULL)*3600);
}
void log_printf(const char *fmt,...) {
    CHECK(!strstr(fmt,"http"));va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);
}
int main(int argc,char **argv) {
    if(argc<2||argc>3){fprintf(stderr,"Usage: dash_demux_test <generated DASH fixture directory> [shift]\n");return 2;}
    shifted=argc==3;
    fixture=argv[1];av_log_set_level(AV_LOG_ERROR);
    VtIptvSourceConfig config={0};vt_dash_source_configure(&config);
    VtIptvSession *session=vt_iptv_session_create("https://fixture.test/index.mpd",&config);CHECK(session);
    VtDecoderStreamFactory factory;vt_iptv_session_factory(session,&factory);
    Input video={0},audio={0};open_input(&video,&factory);open_input(&audio,&factory);
    int64_t origin=video.format->start_time;CHECK(origin!=AV_NOPTS_VALUE&&origin>=0);
    CHECK(llabs(audio.format->start_time-origin)<1000);
    Decode v={0},a={0};decoder_open(&v,&video,AVMEDIA_TYPE_VIDEO,origin);decoder_open(&a,&audio,AVMEDIA_TYPE_AUDIO,origin);
    int vr=1,ar=1;
    while(vr||ar){if(vr)vr=decode_step(&v,&video);if(ar)ar=decode_step(&a,&audio);}
    fprintf(stderr,"DASH decoded video=%u audio=%u first=%lld/%lld last=%lld/%lld\n",v.frames,a.frames,
        (long long)v.first,(long long)a.first,(long long)v.last,(long long)a.last);
    CHECK(v.frames==360);CHECK(a.frames>=560);CHECK(llabs(v.first-a.first)<50000);
    CHECK(v.last>11900000&&a.last>11900000&&llabs(v.last-a.last)<100000);
    CHECK(!vt_iptv_session_event(session));
    VtIptvSourceStatus status;vt_iptv_session_status(session,&status);CHECK(status.finite&&status.duration_ms==12000);
    close_input(&audio);close_input(&video);vt_iptv_session_destroy(session);
    CHECK(requests==16); /* One MPD, two init fragments, six video + seven audio. */
    CHECK(!shifted||shifted_fragments==13);
    printf("DASH FFmpeg %s: %u checks, %u video / %u audio frames, A/V delta %lld us; %u requests\n",
        av_version_info(),checks,v.frames,a.frames,(long long)(v.first-a.first),requests);
    av_frame_free(&a.frame);av_frame_free(&v.frame);avcodec_free_context(&a.codec);avcodec_free_context(&v.codec);
    return 0;
}
