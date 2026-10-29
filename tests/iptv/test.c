#include "network/iptv_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita_https.h>
#include <pthread.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
static const char media[] = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:2,\na.ts\n#EXTINF:2,\nb.ts\n#EXTINF:2,\nc.ts\n";
static const char master[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=640x360,CODECS=\"avc1.42e01e,mp4a.40.2\"\nmedia.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720,CODECS=\"avc1.64001f,mp4a.40.2\"\nhd.m3u8\n";
static const char discontinuity[] = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:2,\na.ts\n#EXT-X-DISCONTINUITY\n#EXTINF:2,\nb.ts\n";
typedef struct { const char *playlist; unsigned calls, segments; int failure, cancel; const char *master; } Mock;
static VtIptvSource *source(Mock *m, size_t cap);
static int mock_fetch(void *ctx, const char *url, size_t limit, unsigned char **data,
    size_t *size, char *effective, size_t capacity, volatile int *cancel) {
    Mock *m = ctx; m->calls++; *data = NULL; *size = 0;
    if (m->cancel) { *cancel = 1; return VT_IPTV_CANCELLED; }
    if (m->failure) return m->failure;
    snprintf(effective, capacity, "%s", url);
    if (strstr(url, ".m3u8")) {
        const char *body = strstr(url,"master.m3u8") ? (m->master?m->master:master) : m->playlist;
        *size = strlen(body); CHECK(*size <= limit); *data = malloc(*size); CHECK(*data); memcpy(*data, body, *size);
    } else {
        CHECK(limit >= 376); *size = 376; *data = malloc(*size); CHECK(*data);
        memset(*data, strstr(url,"a.ts") ? 1 : strstr(url,"b.ts") ? 2 : 3, *size);
        (*data)[0] = (*data)[188] = 0x47; m->segments++;
    }
    return VT_IPTV_OK;
}
static void parser_tests(void) {
    VtIptvManifest *m = calloc(1, sizeof(*m)); CHECK(m);
    CHECK(!vt_iptv_manifest_parse(media, strlen(media), "https://example.org/live/index.m3u8", m));
    CHECK(m->segment_count == 3 && !m->variant_count && m->target_ms == 2000 && !m->ended);
    CHECK(m->segments[0].sequence == 10 && m->segments[2].sequence == 12);
    CHECK(!strcmp(m->segments[0].url, "https://example.org/live/a.ts"));
    CHECK(!vt_iptv_manifest_parse(master, strlen(master), "https://example.org/live/master.m3u8", m));
    CHECK(m->variant_count == 2 && vt_iptv_manifest_variant(m,4000000,1280,720) == 0);
    m->variants[0].width=1920;m->variants[0].height=1080;
    CHECK(vt_iptv_manifest_variant(m,4000000,1280,720)==1);
    m->variants[1].width=1920;m->variants[1].height=1080;
    CHECK(vt_iptv_manifest_variant(m,4000000,1280,720)<0);
    CHECK(vt_iptv_manifest_variant(m,500000,1280,720) < 0);
    const char *optional="#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"English\",URI=\"subs.m3u8\"\n#EXT-X-STREAM-INF:BANDWIDTH=1539795,SUBTITLES=\"subs\"\nmedia.m3u8\n";
    CHECK(!vt_iptv_manifest_parse(optional,strlen(optional),"https://example.org/master.m3u8",m));
    CHECK(vt_iptv_manifest_variant(m,4000000,1280,720)==0 && !m->variants[0].codecs_declared && m->variants[0].subtitle_group);
    const char *external="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=720x408,CODECS=\"avc1.4d401f,mp4a.40.2\",AUDIO=\"separate\"\nmedia.m3u8\n";
    CHECK(!vt_iptv_manifest_parse(external,strlen(external),"https://example.org/master.m3u8",m));
    CHECK(m->variants[0].external_av && vt_iptv_manifest_variant(m,4000000,1280,720)<0);
    const char *spaced="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=720x408,CODECS=\"avc1.4d401f, mp4a.40.2\"\nmedia.m3u8\n";
    CHECK(!vt_iptv_manifest_parse(spaced,strlen(spaced),"https://example.org/master.m3u8",m));
    CHECK(vt_iptv_manifest_variant(m,4000000,1280,720)==0);
    const char *hevc="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000,CODECS=\"hvc1.1.6.L93.B0,mp4a.40.2\"\nmedia.m3u8\n";
    CHECK(!vt_iptv_manifest_parse(hevc,strlen(hevc),"https://example.org/master.m3u8",m));
    CHECK(vt_iptv_manifest_variant(m,4000000,1280,720)<0);
    const char *encrypted="#EXTM3U\n#EXT-X-TARGETDURATION:5\n#EXT-X-KEY:METHOD=AES-128,URI=\"key\"\n#EXTINF:5,\na.ts\n";
    CHECK(vt_iptv_manifest_parse(encrypted,strlen(encrypted),"https://example.org/live.m3u8",m)==VT_IPTV_ENCRYPTED);
    char url[VT_IPTV_URL_MAX];
    CHECK(!vt_iptv_resolve_url("https://example.org/live/m.m3u8", "../a.ts?token=abc", url, sizeof(url)));
    CHECK(!strcmp(url,"https://example.org/a.ts?token=abc"));
    CHECK(!vt_iptv_resolve_url("http://192.168.1.8/live/m.m3u8", "a.ts", url, sizeof(url)));
    const char *bad_urls[] = {"http://example.org/a", "file:///etc/passwd", "https://user:pass@example.org/a", "a.ts#fragment", "a\n.ts", "https://example.org\\a", "data:text/plain,x"};
    for (unsigned i=0;i<sizeof(bad_urls)/sizeof(*bad_urls);i++) CHECK(vt_iptv_resolve_url("https://example.org/live.m3u8",bad_urls[i],url,sizeof(url))<0);
    const char *invalid[] = {
        "", "<html>error</html>", "#EXTM3U\n#EXTINF:-1,Channel\na.ts\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:nan,\na.ts\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:3,\na.ts\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:18446744073709551615\n#EXTINF:2,\na.ts\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-KEY:METHOD=AES-128,URI=\"key\"\n#EXTINF:2,\na.ts\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2,\na.ts\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-BYTERANGE:188@0\n#EXTINF:2,\na.ts\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-GAP\n#EXTINF:2,\na.ts\n",
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=9,BANDWIDTH=10\na.m3u8\n"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) CHECK(vt_iptv_manifest_parse(invalid[i],strlen(invalid[i]),"https://example.org/m.m3u8",m)<0);
    CHECK(!vt_iptv_manifest_parse(discontinuity,strlen(discontinuity),"https://example.org/m.m3u8",m));
    CHECK(m->segments[0].discontinuity==0 && m->segments[1].discontinuity==1);
    char too_many[8192]="#EXTM3U\n#EXT-X-TARGETDURATION:2\n";
    for(unsigned i=0;i<65;i++)strcat(too_many,"#EXTINF:2,\na.ts\n");
    CHECK(vt_iptv_manifest_parse(too_many,strlen(too_many),"https://example.org/m.m3u8",m)==VT_IPTV_LIMIT);
    /* Exhaust every truncated prefix without out-of-bounds reads. */
    for(size_t size=0;size<strlen(media);size++) (void)vt_iptv_manifest_parse(media,size,"https://example.org/m.m3u8",m);
    const char *extra_bad[]={
        "#EXTM3U\n#EXT-X-VERSION:8\n",
        "#EXTM3U\n#EXT-X-DISCONTINUITY\n#EXT-X-DISCONTINUITY-SEQUENCE:5\n",
        "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-STREAM-INF:BANDWIDTH=99\nm.m3u8\n"};
    for(unsigned i=0;i<3;i++)CHECK(vt_iptv_manifest_parse(extra_bad[i],strlen(extra_bad[i]),"https://example.org/m.m3u8",m)<0);
    unsigned random=1234567;char fuzz[512];
    for(unsigned test=0;test<2000;test++) {
        for(unsigned i=0;i<sizeof(fuzz);i++){random=random*1664525u+1013904223u;fuzz[i]=(char)(random>>24);}
        if(test&1)memcpy(fuzz,"#EXTM3U\n",8);
        CHECK(vt_iptv_manifest_parse(fuzz,sizeof(fuzz),"https://example.org/m.m3u8",m)<0);
    }
    free(m);
}
typedef struct { VtIptvSource *source; VtIptvCursor cursor; int error; } Reader;
static void *concurrent_reader(void *ctx) {
    Reader *reader=ctx;
    for(unsigned i=0;i<10000;i++) {
        unsigned char data[131];size_t size;
        int result=vt_iptv_source_read(reader->source,&reader->cursor,data,sizeof(data),&size);
        VtIptvSourceStatus status;vt_iptv_source_status(reader->source,&status);
        if(size>sizeof(data)||status.buffered_bytes>752||result<0)reader->error=1;
        if(result==VT_IPTV_LAGGED||result==VT_IPTV_DISCONTINUITY)vt_iptv_source_begin(reader->source,&reader->cursor);
    }
    return NULL;
}
static void concurrency_test(void) {
    Mock mock={.playlist=media};VtIptvSource *s=source(&mock,752);
    CHECK(!vt_iptv_source_poll(s,0));Reader readers[2]={{.source=s},{.source=s}};
    CHECK(!vt_iptv_source_begin(s,&readers[0].cursor));readers[1].cursor=readers[0].cursor;
    pthread_t threads[2];for(int i=0;i<2;i++)CHECK(!pthread_create(&threads[i],NULL,concurrent_reader,&readers[i]));
    char playlist[512];
    for(unsigned i=1;i<200;i++) {
        snprintf(playlist,sizeof(playlist),"#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:%u\n#EXTINF:2,\na.ts\n",10+i);
        mock.playlist=playlist;CHECK(!vt_iptv_source_poll(s,i*1000));
    }
    for(int i=0;i<2;i++){CHECK(!pthread_join(threads[i],NULL));CHECK(!readers[i].error);}
    vt_iptv_source_cancel(s);vt_iptv_source_destroy(s);
}
static VtIptvSource *source(Mock *m, size_t cap) {
    VtIptvSourceConfig config={.buffer_limit=cap,.segment_limit=376,.fetch=mock_fetch,.fetch_ctx=m};
    VtIptvSource *s=vt_iptv_source_create("https://example.org/media.m3u8",&config);CHECK(s);return s;
}
static void source_tests(void) {
    const char *masters[]={
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000,SUBTITLES=\"optional\"\nmedia.m3u8\n",
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000,AUDIO=\"separate\"\nmedia.m3u8\n",
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=1920x1080\nmedia.m3u8\n"};
    const int expected[]={VT_IPTV_OK,VT_IPTV_RENDITIONS,VT_IPTV_OK};
    for(unsigned i=0;i<3;i++) {
        Mock probe={.playlist=media,.master=masters[i]};
        VtIptvSourceConfig config={.buffer_limit=1128,.segment_limit=376,.fetch=mock_fetch,.fetch_ctx=&probe};
        VtIptvSource *source=vt_iptv_source_create("https://example.org/master.m3u8",&config);CHECK(source);
        CHECK(vt_iptv_source_poll(source,0)==expected[i]);
        CHECK(probe.calls==(i==1?1u:3u));
        if(i==1){CHECK(vt_iptv_source_poll(source,1000)==expected[i] && probe.calls==1);}
        vt_iptv_source_destroy(source);
    }
    Mock encrypted={.playlist="#EXTM3U\n#EXT-X-TARGETDURATION:5\n#EXT-X-KEY:METHOD=AES-128,URI=\"key\"\n#EXTINF:5,\na.ts\n"};
    VtIptvSource *encrypted_source=source(&encrypted,1128);
    CHECK(vt_iptv_source_poll(encrypted_source,0)==VT_IPTV_ENCRYPTED);
    CHECK(vt_iptv_source_poll(encrypted_source,1000)==VT_IPTV_ENCRYPTED && encrypted.calls==1);
    vt_iptv_source_destroy(encrypted_source);
    Mock mock={.playlist=media};VtIptvSource *s=source(&mock,1128);
    VtIptvCursor a,b;unsigned char data[376],second[376];size_t size=999;
    CHECK(vt_iptv_source_begin(s,&a)==VT_IPTV_WAIT);
    CHECK(!vt_iptv_source_poll(s,0));CHECK(!vt_iptv_source_begin(s,&a));b=a;
    CHECK(!vt_iptv_source_read(s,&a,data,100,&size)&&size==100);
    CHECK(!vt_iptv_source_read(s,&b,second,376,&size)&&size==376);
    CHECK(!memcmp(data,second,100));CHECK(mock.segments==1); /* Two consumers, one fetch. */
    CHECK(!vt_iptv_source_read(s,&a,data,376,&size)&&size==276);
    CHECK(!memcmp(data,second+100,276));
    CHECK(vt_iptv_source_read(s,&a,data,376,&size)==VT_IPTV_WAIT&&size==0);
    CHECK(!vt_iptv_source_poll(s,0));CHECK(!vt_iptv_source_poll(s,0));CHECK(mock.segments==3);
    CHECK(vt_iptv_source_poll(s,0)==VT_IPTV_WAIT&&mock.segments==3);
    CHECK(vt_iptv_source_poll(s,1000)==VT_IPTV_WAIT&&mock.segments==3);
    vt_iptv_source_cancel(s);CHECK(vt_iptv_source_read(s,&a,data,376,&size)==VT_IPTV_CANCELLED);
    CHECK(vt_iptv_source_poll(s,1001)==VT_IPTV_CANCELLED);vt_iptv_source_destroy(s);

    mock=(Mock){.playlist=media};s=source(&mock,752);CHECK(!vt_iptv_source_poll(s,0));CHECK(!vt_iptv_source_begin(s,&a));
    CHECK(!vt_iptv_source_poll(s,0));CHECK(!vt_iptv_source_poll(s,0));
    CHECK(vt_iptv_source_read(s,&a,data,376,&size)==VT_IPTV_LAGGED && !size);
    VtIptvSourceStatus status;vt_iptv_source_status(s,&status);CHECK(status.buffered_bytes==752 && status.buffered_segments==2);
    CHECK(!vt_iptv_source_begin(s,&a)&&a.sequence==11);vt_iptv_source_destroy(s);

    mock=(Mock){.playlist=discontinuity};s=source(&mock,752);
    CHECK(!vt_iptv_source_poll(s,0));CHECK(!vt_iptv_source_begin(s,&a));CHECK(!vt_iptv_source_poll(s,0));
    CHECK(!vt_iptv_source_read(s,&a,data,376,&size));
    CHECK(vt_iptv_source_read(s,&a,data,376,&size)==VT_IPTV_DISCONTINUITY);
    CHECK(vt_iptv_source_read(s,&a,data,376,&size)==VT_IPTV_DISCONTINUITY);
    CHECK(!vt_iptv_source_reanchor(s,a.sequence,&a));CHECK(!vt_iptv_source_read(s,&a,data,376,&size));
    vt_iptv_source_destroy(s);

    char jumped[512];snprintf(jumped,sizeof(jumped),"%s",media);char *seq=strstr(jumped,"SEQUENCE:10");CHECK(seq);seq[9]='2';
    mock=(Mock){.playlist=media};s=source(&mock,1128);CHECK(!vt_iptv_source_poll(s,0));CHECK(!vt_iptv_source_begin(s,&a));
    mock.playlist=jumped;CHECK(!vt_iptv_source_poll(s,1000));
    CHECK(vt_iptv_source_read(s,&a,data,376,&size)==VT_IPTV_DISCONTINUITY);
    CHECK(!vt_iptv_source_begin(s,&a)&&a.sequence==20);vt_iptv_source_destroy(s);

    mock=(Mock){.playlist=media,.failure=VT_IPTV_IO};s=source(&mock,752);
    CHECK(vt_iptv_source_poll(s,0)==VT_IPTV_WAIT&&mock.calls==1);
    CHECK(vt_iptv_source_poll(s,499)==VT_IPTV_WAIT&&mock.calls==1);
    CHECK(vt_iptv_source_poll(s,500)==VT_IPTV_WAIT&&mock.calls==2);
    CHECK(vt_iptv_source_poll(s,1500)==VT_IPTV_IO&&mock.calls==3);
    CHECK(vt_iptv_source_poll(s,5000)==VT_IPTV_IO&&mock.calls==3);vt_iptv_source_destroy(s);

    char ended[512];snprintf(ended,sizeof(ended),"%s#EXT-X-ENDLIST\n",media);
    mock=(Mock){.playlist=ended};s=source(&mock,1128);
    for(unsigned i=0;i<3;i++)CHECK(!vt_iptv_source_poll(s,0));CHECK(vt_iptv_source_poll(s,0)==VT_IPTV_END);
    CHECK(!vt_iptv_source_begin(s,&a));for(unsigned i=0;i<3;i++)CHECK(!vt_iptv_source_read(s,&a,data,376,&size)&&size==376);
    CHECK(vt_iptv_source_read(s,&a,data,376,&size)==VT_IPTV_END&&size==0);vt_iptv_source_destroy(s);
}

/* Exercise the production HTTP adapter with bounded mock transfers. */
static unsigned http_mode;
static char http_agent[VT_IPTV_USER_AGENT_MAX];
VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *c) {
    CHECK(c->allow_http && !c->pinned_public_key);
    snprintf(http_agent,sizeof(http_agent),"%s",c->user_agent);return (void*)1;
}
void vita_https_client_destroy(VitaHttpsClient *c) { CHECK(c==(void*)1); }
int vita_https_perform(VitaHttpsClient *c,const VitaHttpsRequest *request,VitaHttpsResponse *response) {
    (void)c; memset(response,0,sizeof(*response));response->status_code=http_mode==3?403:200;
    CHECK(!strncmp(request->url,"https://",8));
    if(http_agent[0] && strcmp(http_agent,"VitaMediaDeck/1.4.3")) {
        int found=0;
        for(const char *const *h=request->headers;h && *h;h++)
            if(!strncmp(*h,"User-Agent: ",12)){CHECK(!strcmp(*h+12,http_agent));found++;}
        CHECK(found==1); /* Full validated agent reaches the request, not a 128-byte default. */
    }
    if(http_mode==18)response->status_code=429;
    if(http_mode==19)response->status_code=503;
    snprintf(response->effective_url,sizeof(response->effective_url),"%s",http_mode==2?"http://example.org/downgrade":"https://example.org/final/media.m3u8");
    if(http_mode==4)*request->cancel_flag=1;
    if(http_mode==5){response->status_code=0;return VITA_HTTPS_ERROR_CURL_BASE-6;}
    if(http_mode==6){response->status_code=0;return VITA_HTTPS_ERROR_CURL_BASE-28;}
    if(http_mode==7){response->status_code=0;return VITA_HTTPS_ERROR_CURL_BASE-60;}
    if(http_mode==8){response->status_code=404;return -4;}
    if(http_mode==9){response->status_code=401;return -4;}
    if(http_mode==10){response->status_code=403;return -4;}
    if(http_mode==11){response->status_code=302;return 0;}
    if(http_mode==12){response->status_code=301;return VITA_HTTPS_ERROR_CURL_BASE-47;}
    if(http_mode>=13 && http_mode<=17) {
        CHECK(request->headers && !strcmp(request->headers[0],"Range: bytes=4294967300-4294967315"));
        const char *status=http_mode==17?"HTTP/1.1 200 OK\r\n":"HTTP/1.1 206 Partial Content\r\n";
        const char *range=http_mode==14?"Content-Range: bytes 4294967301-4294967316/5000000000\r\n":
            "Content-Range: bytes 4294967300-4294967315/5000000000\r\n";
        response->status_code=http_mode==17?200:206;
        request->header(status,strlen(status),request->header_opaque);
        if(http_mode!=15)request->header(range,strlen(range),request->header_opaque);
        size_t n=http_mode==16?15:16;
        return request->write(media,n,request->write_opaque)==n?0:-1;
    }
    size_t n=strlen(media);return request->write(media,n,request->write_opaque)==n?0:-1;
}
static void http_tests(void) {
    unsigned char *data;size_t size;char effective[2048];volatile int cancel=0;
    CHECK(!vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel));
    CHECK(size==strlen(media)&&strstr(effective,"/final/"));free(data);
    CHECK(!strcmp(http_agent,"VitaMediaDeck/1.4.3"));
    VtIptvHttpOptions options={.user_agent="HbbTV/1.6.1"};
    CHECK(!vt_iptv_http_fetch(&options,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel));free(data);
    CHECK(!strcmp(http_agent,"HbbTV/1.6.1"));
    memset(options.user_agent,'A',136);options.user_agent[136]=0;
    CHECK(!vt_iptv_http_fetch(&options,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel));free(data);
    CHECK(strlen(http_agent)==136 && !strcmp(http_agent,options.user_agent));
    memset(options.user_agent,'B',255);options.user_agent[255]=0;
    CHECK(!vt_iptv_http_fetch(&options,"HTTPS://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel));free(data);
    CHECK(strlen(http_agent)==255);
    strcpy(options.user_agent,"player\r\nAuthorization: injected");
    CHECK(vt_iptv_http_fetch(&options,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_INVALID);
    CHECK(!data&&!size);
    CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",10,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_LIMIT);free(data);
    http_mode=2;CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_INVALID);free(data);
    http_mode=3;CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_ACCESS);free(data);
    http_mode=4;CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_CANCELLED);free(data);
    cancel=0;
    const int expected[]={VT_IPTV_DNS,VT_IPTV_TIMEOUT,VT_IPTV_TLS,VT_IPTV_NOT_FOUND,VT_IPTV_ACCESS,VT_IPTV_ACCESS,VT_IPTV_REDIRECT,VT_IPTV_REDIRECT};
    for(http_mode=5;http_mode<=12;http_mode++) {
        CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel)==expected[http_mode-5]);free(data);
    }
    for(http_mode=18;http_mode<=19;http_mode++) {
        int r=vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel);
        CHECK(r==(http_mode==18?VT_IPTV_HTTP_RATE_LIMIT:VT_IPTV_HTTP_SERVER));free(data);
    }
    for(http_mode=13;http_mode<=17;http_mode++) {
        int r=vt_iptv_http_fetch_range(NULL,"https://example.org/video.mp4",4294967300ULL,16,&data,&size,effective,sizeof(effective),&cancel);
        CHECK(http_mode==13?r==0:r==VT_IPTV_RANGE);free(data);
    }
}
static void local_http_tests(void){
 char temp[]="/tmp/vmd-local-fetch.XXXXXX",previous[4096];CHECK(getcwd(previous,sizeof(previous)));CHECK(mkdtemp(temp)&&!chdir(temp));
 FILE *f=fopen("ux0:channel.m3u","wb");CHECK(f);CHECK(fwrite(media,1,strlen(media),f)==strlen(media));CHECK(!fclose(f));
 VtIptvHttpOptions options={.allow_local=1};unsigned char *data=NULL;size_t size;char effective[2048];volatile int cancel=0;
 CHECK(vt_iptv_http_fetch(NULL,"ux0:channel.m3u",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_INVALID&&!data);
 CHECK(!vt_iptv_http_fetch(&options,"ux0:channel.m3u",4096,&data,&size,effective,sizeof(effective),&cancel));CHECK(size==strlen(media)&&!memcmp(data,media,size));free(data);
 CHECK(vt_iptv_http_fetch(&options,"ux0:channel.m3u",8,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_LIMIT&&!data);
 CHECK(!vt_iptv_http_fetch_range(&options,"ux0:channel.m3u",2,10,&data,&size,effective,sizeof(effective),&cancel));CHECK(size==10&&!memcmp(data,media+2,10));free(data);
 CHECK(vt_iptv_http_fetch_range(&options,"ux0:channel.m3u",strlen(media),10,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_RANGE&&!data);
 CHECK(vt_iptv_http_fetch(&options,"ux0:missing.m3u",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_NOT_FOUND);
 cancel=1;CHECK(vt_iptv_http_fetch(&options,"ux0:channel.m3u",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_CANCELLED&&!data);
 CHECK(!unlink("ux0:channel.m3u")&&!chdir(previous)&&!rmdir(temp));
}
static void prebuffer_tests(void) {
    Mock mock={.playlist=media};
    VtIptvSourceConfig config={.fetch=mock_fetch,.fetch_ctx=&mock,
        .buffer_limit=4096,.segment_limit=376,.startup_segments=3};
    VtIptvSource *s=vt_iptv_source_create("https://example.org/media.m3u8",&config);CHECK(s);
    vt_iptv_source_protect(s,0);
    VtIptvSourceStatus status;
    for(unsigned i=0;i<3;i++) {
        CHECK(vt_iptv_source_poll(s,0)==0);vt_iptv_source_status(s,&status);
        CHECK(status.startup_ready==(i==2));
    }
    vt_iptv_source_cancel(s);vt_iptv_source_status(s,&status);CHECK(!status.startup_ready);
    vt_iptv_source_destroy(s);
    /* A small cache cannot reach the nominal target: release startup when
     * reservation would block, retaining both readers' original anchor. */
    config.buffer_limit=752;mock=(Mock){.playlist=media};
    s=vt_iptv_source_create("https://example.org/media.m3u8",&config);CHECK(s);vt_iptv_source_protect(s,0);
    CHECK(!vt_iptv_source_poll(s,0));CHECK(!vt_iptv_source_poll(s,0));
    CHECK(vt_iptv_source_poll(s,0)==VT_IPTV_WAIT);vt_iptv_source_status(s,&status);
    CHECK(status.startup_ready&&status.buffered_segments==2);vt_iptv_source_destroy(s);
    mock=(Mock){.playlist="#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\na.ts\n#EXT-X-ENDLIST\n"};
    config.buffer_limit=4096;
    s=vt_iptv_source_create("https://example.org/media.m3u8",&config);CHECK(s);
    CHECK(!vt_iptv_source_poll(s,0));vt_iptv_source_status(s,&status);CHECK(!status.startup_ready);
    CHECK(vt_iptv_source_poll(s,0)==VT_IPTV_END);vt_iptv_source_status(s,&status);
    CHECK(status.startup_ready&&status.buffered_segments==1);vt_iptv_source_destroy(s);
}
int main(void) { parser_tests();source_tests();prebuffer_tests();concurrency_test();http_tests();local_http_tests();printf("IPTV source: %u assertions passed\n",checks);return 0; }
