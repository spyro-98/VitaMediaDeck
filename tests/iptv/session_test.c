#include "network/iptv_session.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
typedef struct { int calls, segments, release, blocked, ended, changed; } Mock;
static int get(const int *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void put(int *p, int value) { __atomic_store_n(p, value, __ATOMIC_RELEASE); }
static void tick(void) { struct timespec delay = {0,1000000}; nanosleep(&delay,NULL); }
static int mock_fetch(void *ctx, const char *url, size_t limit, unsigned char **data,
    size_t *size, char *effective, size_t capacity, volatile int *cancel) {
    Mock *m = ctx; __atomic_add_fetch(&m->calls, 1, __ATOMIC_RELAXED);
    snprintf(effective, capacity, "%s", url); *data = NULL; *size = 0;
    if (strstr(url,"m3u8")) {
        char playlist[1024];
        snprintf(playlist,sizeof(playlist),
            "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:10\n"
            "#EXTINF:2,\na.ts\n%s#EXTINF:2,\nb.ts\n#EXTINF:2,\nc.ts\n%s",
            m->changed ? "#EXT-X-DISCONTINUITY\n" : "", m->ended ? "#EXT-X-ENDLIST\n" : "");
        *size=strlen(playlist); assert(*size<=limit); *data=malloc(*size);
        assert(*data); memcpy(*data,playlist,*size);
    } else {
        if (strstr(url,"b.ts")) {
            put(&m->blocked,1);
            while (!get(&m->release) && !__atomic_load_n(cancel,__ATOMIC_ACQUIRE)) tick();
            if (__atomic_load_n(cancel,__ATOMIC_ACQUIRE)) return VT_IPTV_CANCELLED;
        }
        *size=376; assert(*size<=limit); *data=malloc(*size); assert(*data);
        memset(*data,strstr(url,"a.ts")?1:strstr(url,"b.ts")?2:3,*size);
        (*data)[0]=(*data)[188]=0x47;
        __atomic_add_fetch(&m->segments,1,__ATOMIC_RELAXED);
    }
    return 0;
}
int vt_iptv_http_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,
    size_t *size,char *effective,size_t capacity,volatile int *cancel) {
    return mock_fetch(ctx,url,limit,data,size,effective,capacity,cancel);
}
static VtIptvSession *make(Mock *m, VtDecoderStreamFactory *f) {
    VtIptvSourceConfig config={.buffer_limit=1128,.segment_limit=376,
        .fetch=mock_fetch,.fetch_ctx=m};
    VtIptvSession *s=vt_iptv_session_create("https://example.org/m.m3u8",&config);
    CHECK(s); vt_iptv_session_factory(s,f); return s;
}
typedef struct { VtDecoderStreamHandle *handle; int result,done; unsigned char bytes[376]; } Read;
static void *read_wait(void *ctx) {
    Read *r=ctx; r->result=r->handle->read(r->handle->opaque,r->bytes,sizeof(r->bytes));
    put(&r->done,1); return NULL;
}
static void wait_blocked(Mock *m) {
    for(unsigned i=0;i<3000&&!get(&m->blocked);i++)tick();
    CHECK(get(&m->blocked));
}
static void pairing_and_wait(void) {
    Mock m={.ended=1}; VtDecoderStreamFactory f;
    VtIptvSession *s=make(&m,&f); VtDecoderStreamHandle a,b,third;
    CHECK(!f.open(f.opaque,&a)); CHECK(a.size==-1&&!a.seek&&a.abort);
    unsigned char first[376],second[376];
    CHECK(a.read(a.opaque,first,sizeof(first))==376);
    /* Audio opens AFTER the video probe and still sees the original bytes. */
    CHECK(!f.open(f.opaque,&b)); CHECK(b.read(b.opaque,second,sizeof(second))==376);
    CHECK(!memcmp(first,second,376)); CHECK(f.open(f.opaque,&third)<0);
    wait_blocked(&m);
    Read read={.handle=&a}; pthread_t thread; CHECK(!pthread_create(&thread,NULL,read_wait,&read));
    for(int i=0;i<30;i++)tick();
    CHECK(!get(&read.done)); /* Temporary starvation is NOT EOF. */
    put(&m.release,1); CHECK(!pthread_join(thread,NULL)); CHECK(read.result==376);
    CHECK(b.read(b.opaque,second,sizeof(second))==376&&!memcmp(read.bytes,second,376));
    CHECK(a.read(a.opaque,first,sizeof(first))==376);
    CHECK(b.read(b.opaque,second,sizeof(second))==376&&!memcmp(first,second,376));
    CHECK(a.read(a.opaque,first,sizeof(first))==0); CHECK(b.read(b.opaque,second,sizeof(second))==0);
    CHECK(get(&m.segments)==3 && !vt_iptv_session_event(s));
    a.close(a.opaque); b.close(b.opaque); vt_iptv_session_destroy(s);
}
static void cancellation_and_reopen(void) {
    Mock m={0}; VtDecoderStreamFactory f; VtIptvSession *s=make(&m,&f);
    VtDecoderStreamHandle a,b; CHECK(!f.open(f.opaque,&a)); CHECK(!f.open(f.opaque,&b));
    unsigned char bytes[376]; CHECK(a.read(a.opaque,bytes,sizeof(bytes))==376);
    wait_blocked(&m); Read read={.handle=&a};pthread_t thread;
    CHECK(!pthread_create(&thread,NULL,read_wait,&read));
    for(int i=0;i<15;i++)tick(); a.abort(a.opaque);
    CHECK(!pthread_join(thread,NULL)&&read.result<0);
    CHECK(!vt_iptv_session_event(s)); /* Cursor abort doesn't poison source/fallback. */
    a.close(a.opaque); b.close(b.opaque);
    CHECK(!f.open(f.opaque,&a)); CHECK(!f.open(f.opaque,&b));
    CHECK(a.read(a.opaque,bytes,sizeof(bytes))==376&&bytes[1]==1);
    vt_iptv_session_cancel(s); CHECK(b.read(b.opaque,bytes,sizeof(bytes))<0);
    a.close(a.opaque); b.close(b.opaque); vt_iptv_session_destroy(s);
}
static void discontinuity(void) {
    Mock m={.release=1,.changed=1}; VtDecoderStreamFactory f; VtIptvSession *s=make(&m,&f);
    VtDecoderStreamHandle a,b; CHECK(!f.open(f.opaque,&a)); CHECK(!f.open(f.opaque,&b));
    unsigned char bytes[376]; CHECK(a.read(a.opaque,bytes,sizeof(bytes))==376);
    CHECK(a.read(a.opaque,bytes,sizeof(bytes))<0);
    CHECK(vt_iptv_session_event(s)==VT_IPTV_DISCONTINUITY);
    CHECK(b.read(b.opaque,bytes,sizeof(bytes))<0); /* Both stop, no unilateral clock jump. */
    a.close(a.opaque); b.close(b.opaque); vt_iptv_session_destroy(s);
}
static void backpressure(void) {
    Mock m={.release=1}; VtIptvSourceConfig cfg={.buffer_limit=752,.segment_limit=376,.fetch=mock_fetch,.fetch_ctx=&m};
    VtIptvSource *s=vt_iptv_source_create("https://example.org/m.m3u8",&cfg);CHECK(s);
    vt_iptv_source_protect(s,10);
    CHECK(!vt_iptv_source_poll(s,0)); CHECK(!vt_iptv_source_poll(s,0));
    for(int i=0;i<10;i++)CHECK(vt_iptv_source_poll(s,0)==VT_IPTV_WAIT);
    CHECK(get(&m.segments)==2&&get(&m.calls)==3);
    vt_iptv_source_protect(s,11);CHECK(!vt_iptv_source_poll(s,0));
    CHECK(get(&m.segments)==3);vt_iptv_source_destroy(s);
}
int main(void) {
    pairing_and_wait(); cancellation_and_reopen(); discontinuity(); backpressure();
    printf("IPTV session: %u assertions passed\n",checks); return 0;
}
