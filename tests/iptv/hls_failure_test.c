/* Actual HLS probe/remux errors, using locally generated incompatible media. */
#define main unused_demux_main
#include "demux_test.c"
#undef main
#include "network/hls_source.h"
int main(int argc,char **argv) {
    CHECK(argc==2);fixture=argv[1];av_log_set_level(AV_LOG_QUIET);
    const struct {const char *file;int expected;} cases[]={
        {"bad-video.m3u8",VT_IPTV_VIDEO_CODEC},
        {"bad-audio.m3u8",VT_IPTV_AUDIO_CODEC},
        {"too-large.m3u8",VT_IPTV_VIDEO_SIZE},
        {"multichannel.m3u8",VT_IPTV_AUDIO_FORMAT},
        {"not-playlist.m3u8",VT_IPTV_MANIFEST}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        VtIptvSourceConfig config={0};vt_hls_source_configure(&config);
        char url[2048];snprintf(url,sizeof(url),"https://fixture.test/%s",cases[i].file);
        VtIptvSource *s=vt_iptv_source_create(url,&config);CHECK(s);
        int result=vt_iptv_source_poll(s,0);
        if(result!=cases[i].expected)fprintf(stderr,"%s: got %d expected %d\n",cases[i].file,result,cases[i].expected);
        CHECK(result==cases[i].expected);
        VtIptvSourceStatus status;vt_iptv_source_status(s,&status);
        CHECK(status.terminal==result&&status.last_error==result&&!status.buffered_segments);
        unsigned before=requests;CHECK(vt_iptv_source_poll(s,1)==result);CHECK(requests==before);
        vt_iptv_source_destroy(s);
    }
    printf("HLS failure classes: %u checks; actual MPEG-2, AC-3, 1080p, AAC 5.1 and HTML input rejected with distinct reasons.\n",checks);
    return 0;
}
