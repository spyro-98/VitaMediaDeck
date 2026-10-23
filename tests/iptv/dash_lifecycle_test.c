#define main unused_demux_main
#include "demux_test.c"
#undef main
#include "network/dash_source.h"
#include <libxml/parser.h>
#include <libavutil/time.h>
#include <unistd.h>

static int variant,manifest_reads,blocked,closed;
void log_printf(const char *fmt,...){(void)fmt;}
static int fetch_fixture(void *ctx,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective,size_t capacity,volatile int *cancel) {
    if(variant==4) {
        __atomic_store_n(&blocked,1,__ATOMIC_RELEASE);
        while(!__atomic_load_n(cancel,__ATOMIC_ACQUIRE))usleep(1000);
        return VT_IPTV_CANCELLED;
    }
    int r=vt_iptv_http_fetch(ctx,url,limit,data,size,effective,capacity,cancel);if(r)return r;
    if(strstr(url,".mpd")) {
        manifest_reads++;
        if(variant==1){memcpy(*data,"<invalid",8);*size=8;return 0;}
        if(variant==2||variant==3) {
            xmlDoc *doc=xmlReadMemory((char*)*data,(int)*size,NULL,NULL,XML_PARSE_NONET);CHECK(doc);
            xmlNode *root=xmlDocGetRootElement(doc);
            xmlSetProp(root,BAD_CAST"type",BAD_CAST(variant==2&&manifest_reads>=3?"static":"dynamic"));
            xmlSetProp(root,BAD_CAST"availabilityStartTime",BAD_CAST"1970-01-01T00:00:00Z");
            xmlSetProp(root,BAD_CAST"minimumUpdatePeriod",BAD_CAST"PT0.5S");
            if(variant==3&&manifest_reads>=2)for(xmlNode *n=root->children;n;n=n->next)
                if(!xmlStrcmp(n->name,BAD_CAST"Period"))xmlSetProp(n,BAD_CAST"id",BAD_CAST"changed");
            xmlChar *bytes=NULL;int n=0;xmlDocDumpMemory(doc,&bytes,&n);CHECK(n>0);
            free(*data);*data=malloc(n);CHECK(*data);memcpy(*data,bytes,n);*size=n;xmlFree(bytes);xmlFreeDoc(doc);
        }
    } else if(variant==5&&strstr(url,"chunk-stream"))*size=16;
    return 0;
}
static VtIptvSource *make_source(int kind) {
    variant=kind;manifest_reads=0;
    VtIptvSourceConfig config={.fetch=fetch_fixture};vt_dash_source_configure(&config);
    VtIptvSource *s=vt_iptv_source_create("https://fixture.test/index.mpd",&config);CHECK(s);return s;
}
static void *fake_open(const char *url,const VtIptvSourceConfig *config){(void)url;(void)config;return calloc(1,1);}
static int fake_next(void *p,uint64_t now,volatile int *cancel,unsigned char **data,size_t *size,int *finite,uint64_t *duration) {
    (void)p;(void)now;(void)cancel;*data=calloc(1,188);if(!*data)return VT_IPTV_MEMORY;
    (*data)[0]=0x47;*size=188;*finite=0;*duration=0;return 0;
}
static void fake_close(void *p){free(p);closed++;}
int main(int argc,char **argv) {
    CHECK(argc==2);fixture=argv[1];av_log_set_level(AV_LOG_QUIET);
    VtIptvSource *s=make_source(1);CHECK(vt_iptv_source_poll(s,0)==VT_IPTV_INVALID);vt_iptv_source_destroy(s);
    s=make_source(5);CHECK(vt_iptv_source_poll(s,0)<0);vt_iptv_source_destroy(s);
    s=make_source(2);int result=0;VtIptvSourceStatus status;
    for(unsigned i=0;i<30;i++) {
        result=vt_iptv_source_poll(s,i*1000);vt_iptv_source_status(s,&status);
        if(i==0)CHECK(!status.finite&&status.audio_sample_rate==48000&&status.audio_channels==2);
        if(result==VT_IPTV_END)break;CHECK(result==0||result==VT_IPTV_WAIT);
    }
    CHECK(result==VT_IPTV_END&&status.finite&&status.duration_ms==12000&&manifest_reads>=3);vt_iptv_source_destroy(s);
    s=make_source(3);CHECK(vt_iptv_source_poll(s,0)==0);CHECK(vt_iptv_source_poll(s,1000)==VT_IPTV_DISCONTINUITY);vt_iptv_source_destroy(s);
    VtIptvSourceConfig config={.segment_limit=188,.buffer_limit=1024*1024,.startup_segments=6,
        .producer={.open=fake_open,.next=fake_next,.close=fake_close}};
    s=vt_iptv_source_create("https://fixture.test/index.mpd",&config);CHECK(s);vt_iptv_source_protect(s,0);
    for(unsigned i=0;i<32;i++) {
        CHECK(vt_iptv_source_poll(s,i)==0);vt_iptv_source_status(s,&status);
        CHECK(status.startup_ready==(i>=5));
    }
    CHECK(vt_iptv_source_poll(s,32)==VT_IPTV_WAIT);vt_iptv_source_status(s,&status);
    CHECK(status.buffered_segments==32&&status.buffered_bytes==32*188);
    VtIptvCursor a,b;CHECK(vt_iptv_source_begin(s,&a)==0);b=a;
    unsigned char x[188],y[188];size_t n,m;
    CHECK(vt_iptv_source_read(s,&a,x,sizeof(x),&n)==0);CHECK(vt_iptv_source_poll(s,33)==VT_IPTV_WAIT);
    CHECK(vt_iptv_source_read(s,&b,y,sizeof(y),&m)==0&&n==m&&!memcmp(x,y,n));
    vt_iptv_source_protect(s,b.sequence);CHECK(vt_iptv_source_poll(s,34)==0);
    vt_iptv_source_cancel(s);CHECK(vt_iptv_source_read(s,&a,x,sizeof(x),&n)==VT_IPTV_CANCELLED);
    vt_iptv_source_destroy(s);CHECK(closed==1);
    variant=4;config=(VtIptvSourceConfig){.fetch=fetch_fixture};vt_dash_source_configure(&config);
    VtIptvSession *session=vt_iptv_session_create("https://fixture.test/index.mpd",&config);CHECK(session);
    int64_t deadline=av_gettime()+1000000;
    while(!__atomic_load_n(&blocked,__ATOMIC_ACQUIRE)&&av_gettime()<deadline)usleep(1000);
    CHECK(__atomic_load_n(&blocked,__ATOMIC_ACQUIRE));int64_t start=av_gettime();
    vt_iptv_session_cancel(session);vt_iptv_session_destroy(session);CHECK(av_gettime()-start<200000);
    printf("DASH lifecycle: %u checks; live refresh/end, epoch change, 32-chunk backpressure and cancellation passed\n",checks);
    return 0;
}
