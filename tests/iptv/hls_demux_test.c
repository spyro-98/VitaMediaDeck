#define main legacy_demux_main
#include "demux_test.c"
#undef main
#include "network/hls_source.h"
int main(int argc,char **argv) {
    CHECK(argc==3);fixture=argv[1];expect_zero_decode_lead=!strncmp(argv[2],"nodelay",7);av_log_set_level(AV_LOG_ERROR);
    char url[2048];snprintf(url,sizeof(url),"https://fixture.test/%s",argv[2]);
    VtIptvSourceConfig config={.buffer_limit=24u*1024u*1024u,.startup_segments=6};
    vt_hls_source_configure(&config);
    VtIptvSession *session=vt_iptv_session_create(url,&config);CHECK(session);
    VtIptvSourceStatus status;int64_t deadline=av_gettime_relative()+20000000;
    do {vt_iptv_session_status(session,&status);
        if(status.terminal<0)fprintf(stderr,"HLS terminal=%d error=%d\n",status.terminal,status.last_error);
        CHECK(status.terminal>=0 && av_gettime_relative()<deadline);
        if(!status.startup_ready)av_usleep(5000);
    } while(!status.startup_ready);
    VtDecoderStreamFactory factory;vt_iptv_session_factory(session,&factory);
    Input video={0},audio={0};open_input(&video,&factory);open_input(&audio,&factory);
    int64_t origin=video.format->start_time;CHECK(origin!=AV_NOPTS_VALUE);
    CHECK(llabs(audio.format->start_time-origin)<1000);
    Decode v={0},a={0};decoder_open(&v,&video,AVMEDIA_TYPE_VIDEO,origin);decoder_open(&a,&audio,AVMEDIA_TYPE_AUDIO,origin);
    int ve=0,ae=0;
    while(!ve || !ae) {
        if(!ve && (ae || v.last<=a.last))ve=!decode_step(&v,&video);
        else if(!ae)ae=!decode_step(&a,&audio);
    }
    CHECK(v.frames>=290 && a.frames>=550);CHECK(llabs(v.first-a.first)<100000);
    CHECK(llabs(v.last-a.last)<150000);CHECK(!vt_iptv_session_event(session));
    printf("HLS %s: %u checks; %u video / %u audio frames; initial delta=%lld final delta=%lld us\n",argv[2],
        __atomic_load_n(&checks,__ATOMIC_RELAXED),v.frames,a.frames,(long long)(v.first-a.first),(long long)(v.last-a.last));
    close_input(&audio);close_input(&video);vt_iptv_session_destroy(session);
    av_frame_free(&a.frame);av_frame_free(&v.frame);avcodec_free_context(&a.codec);avcodec_free_context(&v.codec);
    return 0;
}
