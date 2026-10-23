/* Opt-in real HTTP(S) smoke test. Bounded compressed data remains in RAM;
 * nothing is played, recorded or written to disk. Host curl is not Vita TLS. */
#include "network/iptv_source.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { unsigned char *data; size_t size, limit; volatile int *cancel; } Buffer;
static unsigned requests;
static size_t receive(char *data, size_t size, size_t count, void *ctx) {
    Buffer *b=ctx;if(size && count>SIZE_MAX/size)return 0;size*=count;
    if(*b->cancel||size>b->limit-b->size)return 0;
    void *next=realloc(b->data,b->size+size);if(!next&&size)return 0;
    b->data=next;if(size)memcpy(b->data+b->size,data,size);b->size+=size;return size;
}
static int progress(void *ctx,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d) {
    (void)a;(void)b;(void)c;(void)d;return *(volatile int*)ctx!=0;
}
int vt_iptv_http_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,
    size_t *size,char *effective,size_t capacity,volatile int *cancel) {
    const VtIptvHttpOptions *options=ctx;Buffer buffer={.limit=limit,.cancel=cancel};CURL *curl=curl_easy_init();
    if(!curl)return VT_IPTV_MEMORY;
    requests++;curl_easy_setopt(curl,CURLOPT_URL,url);curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,options && options->user_agent[0]?options->user_agent:"VitaMediaDeck/1.4.1");
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,!strncmp(url,"https://",8)?"https":"http,https");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,20L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&buffer);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,(void*)cancel);
    CURLcode result=curl_easy_perform(curl);long status=0;char *final=NULL;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);curl_easy_getinfo(curl,CURLINFO_EFFECTIVE_URL,&final);
    if(final)snprintf(effective,capacity,"%s",final);else effective[0]=0;
    curl_easy_cleanup(curl);*data=buffer.data;*size=buffer.size;
    printf("HTTP request %u: status=%ld bytes=%zu result=%d\n",requests,status,buffer.size,(int)result);
    return result==CURLE_OK&&status==200?VT_IPTV_OK:VT_IPTV_IO;
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"Usage: http_smoke <authorized HLS playlist URL>\n");return 2;}
    if(curl_global_init(CURL_GLOBAL_DEFAULT))return 2;
    VtIptvSource *source=vt_iptv_source_create(argv[1],NULL);if(!source)return 2;
    int result=0;
    for(unsigned i=0;i<3;i++){result=vt_iptv_source_poll(source,i);if(result)break;}
    VtIptvSourceStatus status;vt_iptv_source_status(source,&status);
    printf("Source: result=%d segments=%u buffered=%zu generation=%llu\n",result,status.buffered_segments,status.buffered_bytes,(unsigned long long)status.generation);
    VtIptvCursor a,b;size_t total=0;
    if(!result && !vt_iptv_source_begin(source,&a)) {
        b=a;
        for(;;) {
            unsigned char first[4096],second[4096];size_t n=0,m=0;
            int ar=vt_iptv_source_read(source,&a,first,sizeof(first),&n);
            int br=vt_iptv_source_read(source,&b,second,sizeof(second),&m);
            if(ar!=br||n!=m||memcmp(first,second,n)){result=VT_IPTV_INVALID;break;}
            if(ar==VT_IPTV_WAIT||ar==VT_IPTV_END)break;
            if(ar){result=ar;break;}total+=n;
        }
    }
    printf("Matched two consumer cursors: %zu bytes; total HTTP requests=%u\n",total,requests);
    vt_iptv_source_cancel(source);vt_iptv_source_destroy(source);curl_global_cleanup();
    return result||!total?1:0;
}
