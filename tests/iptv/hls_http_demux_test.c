/* Public-network opt-in: production source + two real FFmpeg demux/decode
 * readers. Host curl is not Vita TLS; bounded compressed data stays in RAM. */
#define VMD_EXTERNAL_FETCH
#define VMD_HLS_NETWORK
#define main unused_hls_demux_main
#include "demux_test.c"
#undef main
#include <curl/curl.h>
#include "network/hls_source.h"
int main(int argc,char **argv) {
    CHECK(argc==2 || argc==3);CHECK(!curl_global_init(CURL_GLOBAL_DEFAULT));
    int seconds=argc==3?atoi(argv[2]):6;CHECK(seconds>=6 && seconds<=300);
    av_log_set_level(AV_LOG_ERROR);
    VtIptvSourceConfig config={.buffer_limit=24u*1024u*1024u,.startup_segments=6};
    vt_hls_source_configure(&config);
    VtIptvSession *session=vt_iptv_session_create(argv[1],&config);CHECK(session);
    VtIptvSourceStatus status;int64_t deadline=av_gettime_relative()+65000000;
    do {
        vt_iptv_session_status(session,&status);
        if(status.terminal<0)fprintf(stderr,"HLS startup failure: terminal=%d last=%d\n",status.terminal,status.last_error);
        CHECK((!status.terminal || status.terminal==VT_IPTV_END)&&av_gettime_relative()<deadline);
        if(!status.startup_ready)av_usleep(5000);
    } while(!status.startup_ready);
    printf("HLS startup reserve: %u chunks / %zu bytes\n",status.buffered_segments,status.buffered_bytes);
    VtDecoderStreamFactory factory;vt_iptv_session_factory(session,&factory);
    Input video={0},audio={0};open_input(&video,&factory);open_input(&audio,&factory);
    int64_t origin=video.format->start_time;CHECK(origin!=AV_NOPTS_VALUE);
    CHECK(llabs(audio.format->start_time-origin)<1000);
    Decode v={0},a={0};decoder_open(&v,&video,AVMEDIA_TYPE_VIDEO,origin);decoder_open(&a,&audio,AVMEDIA_TYPE_AUDIO,origin);
    CHECK(v.codec->width<=1280&&v.codec->height<=720);
    while(v.last<(int64_t)seconds*1000000||a.last<(int64_t)seconds*1000000) {
        if(v.last<=a.last)CHECK(decode_step(&v,&video));else CHECK(decode_step(&a,&audio));
    }
    CHECK(llabs(v.first-a.first)<100000);CHECK(!vt_iptv_session_event(session));
    printf("Real HLS decode: %u checks; %u video / %u audio frames; A/V delta %lld us\n",
        __atomic_load_n(&checks,__ATOMIC_RELAXED),v.frames,a.frames,(long long)(v.first-a.first));
    close_input(&audio);close_input(&video);vt_iptv_session_destroy(session);
    av_frame_free(&a.frame);av_frame_free(&v.frame);avcodec_free_context(&a.codec);avcodec_free_context(&v.codec);
    curl_global_cleanup();return 0;
}
