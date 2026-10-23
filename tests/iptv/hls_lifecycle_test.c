#define main unused_demux_main
#include "demux_test.c"
#undef main
#include "network/hls_source.h"
#include <unistd.h>
static int mode,blocked;
static unsigned range_requests;
static int fetch_fixture(void *ctx,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective,size_t capacity,volatile int *cancel) {
    int block=mode==10 || (mode==11 && strstr(url,".bin")) || (mode==12 && strstr(url,".ts"));
    if(block) {
        __atomic_store_n(&blocked,1,__ATOMIC_RELEASE);
        while(!__atomic_load_n(cancel,__ATOMIC_ACQUIRE))usleep(1000);
        return VT_IPTV_CANCELLED;
    }
    int result=vt_iptv_http_fetch(ctx,url,limit,data,size,effective,capacity,cancel);
    if(result)return result;
    if(mode==1 && strstr(url,".m3u8"))memset(*data,'?',*size);
    if(mode==2)snprintf(effective,capacity,"http://fixture.test/downgrade");
    if(mode==3 && strstr(url,"implicit-0.ts"))(*data)[*size-1]^=0xff;
    return 0;
}
static int fetch_range(void *ctx,const char *url,uint64_t offset,size_t length,unsigned char **data,
    size_t *size,char *effective,size_t capacity,volatile int *cancel) {
    range_requests++;
    int r=fetch_fixture(ctx,url,32u*1024u*1024u,data,size,effective,capacity,cancel);
    if(r)return r;
    CHECK(offset<=*size && length<=*size-offset);
    memmove(*data,*data+offset,length);*size=length;return 0;
}
static VtIptvSourceConfig config(void) {
    VtIptvSourceConfig c={.fetch=fetch_fixture,.fetch_range=fetch_range,.startup_segments=6};
    vt_hls_source_configure(&c);return c;
}
int main(int argc,char **argv) {
    CHECK(argc==2);fixture=argv[1];av_log_set_level(AV_LOG_QUIET);
    for(mode=1;mode<=3;mode++) {
        VtIptvSourceConfig c=config();
        VtIptvSource *s=vt_iptv_source_create("https://fixture.test/implicit.m3u8",&c);CHECK(s);
        CHECK(vt_iptv_source_poll(s,0)<0);vt_iptv_source_destroy(s);
    }
    mode=0;
    for(unsigned repeat=0;repeat<12;repeat++) {
        VtIptvSourceConfig c=config();
        VtIptvSource *s=vt_iptv_source_create("https://fixture.test/maprange.m3u8",&c);CHECK(s);
        CHECK(vt_iptv_source_poll(s,0)==0);VtIptvSourceStatus status;vt_iptv_source_status(s,&status);
        CHECK(status.audio_sample_rate==48000 && status.audio_channels==2);
        CHECK(status.finite && status.duration_ms>=11900 && status.duration_ms<=12100);
        vt_iptv_source_cancel(s);vt_iptv_source_destroy(s);
    }
    CHECK(range_requests>12);
    for(mode=10;mode<=12;mode++) {
        blocked=0;VtIptvSourceConfig c=config();
        VtIptvSession *s=vt_iptv_session_create("https://fixture.test/implicit.m3u8",&c);CHECK(s);
        int64_t deadline=av_gettime_relative()+2000000;
        while(!__atomic_load_n(&blocked,__ATOMIC_ACQUIRE) && av_gettime_relative()<deadline)usleep(1000);
        CHECK(__atomic_load_n(&blocked,__ATOMIC_ACQUIRE));int64_t started=av_gettime_relative();
        vt_iptv_session_cancel(s);vt_iptv_session_destroy(s);
        CHECK(av_gettime_relative()-started<200000);
    }
    printf("HLS lifecycle: %u checks; exact ranges, corrupt input/padding, redirects, repeated teardown and manifest/key/segment cancellation passed\n",checks);
    return 0;
}
