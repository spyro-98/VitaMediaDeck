#include "network/iptv_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita_https.h>
#include <pthread.h>

static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
static const char media[] = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:2,\na.ts\n#EXTINF:2,\nb.ts\n#EXTINF:2,\nc.ts\n";
static const char master[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=640x360,CODECS=\"avc1.42e01e,mp4a.40.2\"\nmedia.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720,CODECS=\"avc1.64001f,mp4a.40.2\"\nhd.m3u8\n";
static const char discontinuity[] = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:2,\na.ts\n#EXT-X-DISCONTINUITY\n#EXTINF:2,\nb.ts\n";
typedef struct { const char *playlist; unsigned calls, segments; int failure, cancel; } Mock;
static VtIptvSource *source(Mock *m, size_t cap);
static int mock_fetch(void *ctx, const char *url, size_t limit, unsigned char **data,
    size_t *size, char *effective, size_t capacity, volatile int *cancel) {
    Mock *m = ctx; m->calls++; *data = NULL; *size = 0;
    if (m->cancel) { *cancel = 1; return VT_IPTV_CANCELLED; }
    if (m->failure) return m->failure;
    snprintf(effective, capacity, "%s", url);
    if (strstr(url, ".m3u8")) {
        const char *body = strstr(url,"master.m3u8") ? master : m->playlist;
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
    CHECK(vt_iptv_manifest_variant(m,500000,1280,720) < 0);
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
VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *c) { CHECK(c->allow_http && !c->pinned_public_key); return (void*)1; }
void vita_https_client_destroy(VitaHttpsClient *c) { CHECK(c==(void*)1); }
int vita_https_perform(VitaHttpsClient *c,const VitaHttpsRequest *request,VitaHttpsResponse *response) {
    (void)c; memset(response,0,sizeof(*response));response->status_code=http_mode==3?403:200;
    snprintf(response->effective_url,sizeof(response->effective_url),"%s",http_mode==2?"http://example.org/downgrade":"https://example.org/final/media.m3u8");
    if(http_mode==4)*request->cancel_flag=1;
    size_t n=strlen(media);return request->write(media,n,request->write_opaque)==n?0:-1;
}
static void http_tests(void) {
    unsigned char *data;size_t size;char effective[2048];volatile int cancel=0;
    CHECK(!vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel));
    CHECK(size==strlen(media)&&strstr(effective,"/final/"));free(data);
    CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",10,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_LIMIT);free(data);
    http_mode=2;CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_INVALID);free(data);
    http_mode=3;CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_IO);free(data);
    http_mode=4;CHECK(vt_iptv_http_fetch(NULL,"https://example.org/media.m3u8",4096,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_CANCELLED);free(data);
}
int main(void) { parser_tests();source_tests();concurrency_test();http_tests();printf("IPTV source: %u assertions passed\n",checks);return 0; }
