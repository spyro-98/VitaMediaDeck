#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "../../src/network/http_audio_stream.c"
static int calls, in_flight, hold, fail;
static int64_t cursor;
static volatile int *stop;
static int get(int *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void put(int *p,int v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static unsigned char byte_at(int64_t n){return (unsigned char)(n*17+n/65521);}
static int read_mock(void *ctx,void *data,size_t size) {
    (void)ctx;put(&in_flight,1);__atomic_add_fetch(&calls,1,__ATOMIC_RELAXED);
    for(int i=0;i<80 || get(&hold);i++) { if(__atomic_load_n(stop,__ATOMIC_ACQUIRE)){put(&in_flight,0);return -1;} usleep(10000); }
    if(get(&fail)){put(&in_flight,0);return -7;}
    for(size_t i=0;i<size;i++)((unsigned char*)data)[i]=byte_at(cursor+i);
    cursor+=size;put(&in_flight,0);return (int)size;
}
static int64_t seek_mock(void *ctx,int64_t offset,int whence){(void)ctx;assert(whence==SEEK_SET);return cursor=offset;}
static void close_mock(void *ctx){(void)ctx;}
void log_printf(const char *fmt,...){(void)fmt;}
int vt_online_url_valid(const char *url){return url&&!strncmp(url,"https://",8);}
VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *c){assert(c->allow_http);return (void*)1;}
void vita_https_client_destroy(VitaHttpsClient *c){(void)c;}
int vita_https_open_range_stream(VitaHttpsClient *client,const char *url,volatile int *cancel,VitaHttpsStream *out) {
    (void)client;(void)url;stop=cancel;
    *out=(VitaHttpsStream){.read=read_mock,.seek=seek_mock,.close=close_mock,.size=16*BLOCK+13};return 0;
}
static void check_bytes(VtDecoderStreamHandle *s,int64_t position,size_t size) {
    unsigned char bytes[65536];assert(size<=sizeof(bytes));
    int n=s->read(s->opaque,bytes,size);assert(n==(int)size);
    for(int i=0;i<n;i++)assert(bytes[i]==byte_at(position+i));
}
int main(void) {
    VtDecoderStreamFactory factory;vt_http_audio_stream_factory("https://example.org/episode.mp3",&factory);
    VtDecoderStreamHandle stream;assert(!factory.open(factory.opaque,&stream));
    check_bytes(&stream,0,65536);usleep(1900000); /* Start playback with a bounded reserve. */
    uint64_t max_wait=0;
    for(int i=1;i<65;i++) {
        uint64_t before=milliseconds();check_bytes(&stream,(int64_t)i*65536,65536);
        uint64_t wait=milliseconds()-before;if(wait>max_wait)max_wait=wait;
        usleep(125000); /* 512 KiB/s consumer; each GET takes 800 ms. */
    }
    assert(max_wait<100); /* Old synchronous path stalls for >=800 ms per block. */
    put(&hold,1);while(!get(&in_flight))usleep(1000);
    assert(stream.seek(stream.opaque,14*BLOCK+19,SEEK_SET)==14*BLOCK+19);
    put(&hold,0);check_bytes(&stream,14*BLOCK+19,65536);
    assert(stream.seek(stream.opaque,-13,SEEK_END)==16*BLOCK);check_bytes(&stream,16*BLOCK,13);
    unsigned char data;assert(stream.read(stream.opaque,&data,1)==0);
    assert(stream.seek(stream.opaque,INT64_MAX,SEEK_CUR)<0);
    assert(stream.seek(stream.opaque,INT64_MIN,SEEK_CUR)<0);
    assert(stream.seek(stream.opaque,7,SEEK_SET)==7);check_bytes(&stream,7,65536);
    put(&hold,1);while(!get(&in_flight))usleep(1000);
    uint64_t before=milliseconds();stream.abort(stream.opaque);stream.close(stream.opaque);assert(milliseconds()-before<200);
    put(&hold,0);put(&fail,1);assert(!factory.open(factory.opaque,&stream));assert(stream.read(stream.opaque,&data,1)<0);stream.close(stream.opaque);
    printf("Prefetch: 800 ms GETs, max steady playback wait %llu ms; seek, EOF, error and abort passed\n",(unsigned long long)max_wait);
}
