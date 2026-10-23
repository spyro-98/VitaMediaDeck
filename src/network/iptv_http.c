#include "network/iptv_source.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <inttypes.h>
#include <vita_https.h>
#include <curl/curl.h>
#include "network/iptv_trace.h"

typedef struct { unsigned char *data; size_t size, capacity, limit; int error; } Response;
static int network_error(int result,long status) {
    if(result==VITA_HTTPS_ERROR_CURL_BASE-CURLE_COULDNT_RESOLVE_HOST)return VT_IPTV_DNS;
    if(result==VITA_HTTPS_ERROR_CURL_BASE-CURLE_OPERATION_TIMEDOUT)return VT_IPTV_TIMEOUT;
    if(result==VITA_HTTPS_ERROR_UNTRUSTED_CERTIFICATE || result==VITA_HTTPS_ERROR_PIN_MISMATCH ||
       result==VITA_HTTPS_ERROR_CURL_BASE-CURLE_PEER_FAILED_VERIFICATION ||
       result==VITA_HTTPS_ERROR_CURL_BASE-CURLE_SSL_CONNECT_ERROR)return VT_IPTV_TLS;
    if(status==401 || status==403)return VT_IPTV_ACCESS;
    if(status==404 || status==410)return VT_IPTV_NOT_FOUND;
    /* The client already follows redirects. A final 3xx has no usable
     * destination (or exceeded its redirect limit), not a decoder failure. */
    if((status>=300 && status<400) || result==VITA_HTTPS_ERROR_CURL_BASE-CURLE_TOO_MANY_REDIRECTS)return VT_IPTV_REDIRECT;
    return VT_IPTV_IO;
}
/* Identify failed servers without persisting signed paths or URL queries. */
static void log_host(const char *url,char out[256]) {
    const char *begin=url?strstr(url,"://"):NULL;
    if(!begin){out[0]=0;return;}begin+=3;
    size_t length=strcspn(begin,"/?#");if(length>255)length=255;
    const char *userinfo=memchr(begin,'@',length);
    if(userinfo){length-=(size_t)(userinfo+1-begin);begin=userinfo+1;}
    for(size_t i=0;i<length;i++)out[i]=(unsigned char)begin[i]>=32 && (unsigned char)begin[i]<127?begin[i]:'_';
    out[length]=0;
}
static size_t receive(const void *bytes, size_t size, void *ctx) {
    Response *r = ctx;
    if (size > r->limit - r->size) { r->error = VT_IPTV_LIMIT; return 0; }
    if (r->size + size > r->capacity) {
        size_t capacity = r->capacity ? r->capacity : 65536;
        while (capacity < r->size + size) capacity *= 2;
        if (capacity > r->limit) capacity = r->limit;
        void *next = realloc(r->data, capacity);
        if (!next) { r->error = VT_IPTV_MEMORY; return 0; }
        r->data = next; r->capacity = capacity;
    }
    if (size) memcpy(r->data + r->size, bytes, size);
    r->size += size; return size;
}
typedef struct { uint64_t first, last; int valid; } RangeHeader;
static size_t range_header(const void *data,size_t size,void *ctx) {
    RangeHeader *r=ctx;char line[256];
    if(size>=sizeof(line))return size;
    memcpy(line,data,size);line[size]=0;
    if(!strncmp(line,"HTTP/",5))r->valid=0;
    if(!strncasecmp(line,"Content-Range:",14))
        r->valid=sscanf(line+14," bytes %" SCNu64 "-%" SCNu64 "/",&r->first,&r->last)==2;
    return size;
}
static int fetch(void *ctx, const char *url, size_t limit, unsigned char **data,
    size_t *size, char *effective, size_t capacity, volatile int *cancel,
    int ranged,uint64_t offset) {
    const VtIptvHttpOptions *options=ctx;
    if (!data || !size || !effective || !capacity || !limit || limit > 32u * 1024u * 1024u) return VT_IPTV_INVALID;
    *data = NULL; *size = 0; effective[0] = 0;
    char checked[VT_IPTV_URL_MAX];
    if (vt_iptv_resolve_url(url, url, checked, sizeof(checked))) return VT_IPTV_INVALID;
    if (cancel && *cancel) return VT_IPTV_CANCELLED;
    VitaHttpsClientConfig config = {.user_agent = "VitaMediaDeck/1.4.1", .allow_http = 1,
        .connect_timeout_ms = 10000, .request_timeout_ms = 20000};
    if(options && options->user_agent[0]) {
        size_t n=strnlen(options->user_agent,sizeof(options->user_agent));
        if(n==sizeof(options->user_agent))return VT_IPTV_INVALID;
        for(size_t i=0;i<n;i++)if((unsigned char)options->user_agent[i]<32 || (unsigned char)options->user_agent[i]>126)return VT_IPTV_INVALID;
        config.user_agent=options->user_agent;
    }
    VitaHttpsClient *client = vita_https_client_create(&config);
    if (!client) return VT_IPTV_MEMORY;
    Response response = {.limit = limit};
    VitaHttpsRequest request = {.method = "GET", .url = url, .write = receive,
        .write_opaque = &response, .cancel_flag = cancel};
    RangeHeader range={0};char range_text[96];const char *headers[]={range_text,NULL};
    if(ranged) {
        if(offset>UINT64_MAX-limit){vita_https_client_destroy(client);return VT_IPTV_INVALID;}
        snprintf(range_text,sizeof(range_text),"Range: bytes=%" PRIu64 "-%" PRIu64,offset,offset+limit-1);
        request.headers=headers;request.header=range_header;request.header_opaque=&range;
    }
    VitaHttpsResponse metadata = {0};
    uint64_t started=iptv_trace_ms();
    int result = vita_https_perform(client, &request, &metadata);
    if(result<0 || metadata.status_code!=(ranged?206:200) || response.error) {
        char host[256],final_host[256];log_host(url,host);log_host(metadata.effective_url,final_host);
        IPTV_TRACE("IPTV request: host=%s final_host=%s result=%d status=%ld bytes=%u limit=%u receive_error=%d cancelled=%d elapsed=%llu ms\n",
            host,final_host,
            result,metadata.status_code,(unsigned)response.size,(unsigned)limit,response.error,
            cancel&&*cancel,(unsigned long long)(iptv_trace_ms()-started));
    }
    (void)started;
    vita_https_client_destroy(client);
    *data = response.data; *size = response.size;
    if (cancel && *cancel) return VT_IPTV_CANCELLED;
    if (response.error) return response.error;
    if (result < 0 || metadata.status_code != (ranged?206:200)) return network_error(result,metadata.status_code);
    if(ranged && (!range.valid || range.first!=offset || range.last!=offset+limit-1 || response.size!=limit))return VT_IPTV_INVALID;
    return vt_iptv_resolve_url(url, metadata.effective_url, effective, capacity);
}
int vt_iptv_http_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,
    size_t *size,char *effective,size_t capacity,volatile int *cancel) {
    return fetch(ctx,url,limit,data,size,effective,capacity,cancel,0,0);
}
int vt_iptv_http_fetch_range(void *ctx,const char *url,uint64_t offset,size_t length,
    unsigned char **data,size_t *size,char *effective,size_t capacity,volatile int *cancel) {
    return fetch(ctx,url,length,data,size,effective,capacity,cancel,1,offset);
}
