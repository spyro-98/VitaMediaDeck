/* Opt-in real HTTPS over host curl. No media is saved or rendered. */
#define VMD_EXTERNAL_FETCH
#define main unused_hls_demux_main
#include "demux_test.c"
#undef main
#include "network/dash_source.h"
#include <stdarg.h>
#include <curl/curl.h>
void log_printf(const char *fmt,...) {
    CHECK(!strstr(fmt,"http"));va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"Usage: dash_http_smoke <public DASH URL>\n");return 2;}
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT)==0);av_log_set_level(AV_LOG_ERROR);
    VtIptvSourceConfig config={.buffer_limit=24u*1024u*1024u,.startup_segments=6};vt_dash_source_configure(&config);
    VtIptvSession *session=vt_iptv_session_create(argv[1],&config);CHECK(session);
    VtIptvSourceStatus startup;int64_t deadline=av_gettime_relative()+65000000;
    do {
        vt_iptv_session_status(session,&startup);
        CHECK(startup.terminal>=0&&av_gettime_relative()<deadline);
        if(!startup.startup_ready)av_usleep(5000);
    } while(!startup.startup_ready);
    printf("DASH startup reserve: %u chunks / %zu bytes\n",startup.buffered_segments,startup.buffered_bytes);
    VtDecoderStreamFactory factory;vt_iptv_session_factory(session,&factory);
    Input video={0},audio={0};open_input(&video,&factory);open_input(&audio,&factory);
    int64_t origin=video.format->start_time;CHECK(origin!=AV_NOPTS_VALUE&&origin>=0);
    CHECK(llabs(audio.format->start_time-origin)<1000);
    Decode v={0},a={0};decoder_open(&v,&video,AVMEDIA_TYPE_VIDEO,origin);decoder_open(&a,&audio,AVMEDIA_TYPE_AUDIO,origin);
    CHECK(v.codec->width<=1280&&v.codec->height<=720);
    while(v.last<6000000||a.last<6000000) {
        if(v.last<=a.last)CHECK(decode_step(&v,&video));else CHECK(decode_step(&a,&audio));
    }
    CHECK(llabs(v.first-a.first)<100000);CHECK(!vt_iptv_session_event(session));
    VtIptvSourceStatus status;vt_iptv_session_status(session,&status);
    CHECK(status.audio_sample_rate==(unsigned)a.codec->sample_rate);
    CHECK(status.audio_channels==(unsigned)a.codec->ch_layout.nb_channels);
    printf("Real DASH: %u checks; %u video / %u audio frames; first A/V delta %lld us; AAC rate=%u channels=%u; live=%d\n",
        __atomic_load_n(&checks,__ATOMIC_RELAXED),v.frames,a.frames,(long long)(v.first-a.first),status.audio_sample_rate,status.audio_channels,!status.finite);
    close_input(&audio);close_input(&video);vt_iptv_session_destroy(session);
    av_frame_free(&a.frame);av_frame_free(&v.frame);avcodec_free_context(&a.codec);avcodec_free_context(&v.codec);
    curl_global_cleanup();return 0;
}
