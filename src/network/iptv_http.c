#include "network/iptv_source.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
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
    if(status==429)return VT_IPTV_HTTP_RATE_LIMIT;
    if(status>=500 && status<=599)return VT_IPTV_HTTP_SERVER;
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
    if(vt_iptv_local_path(url)){
        if(!options||!options->allow_local||vt_iptv_resource_resolve(url,url,effective,capacity))return VT_IPTV_INVALID;
        if(cancel&&*cancel)return VT_IPTV_CANCELLED;
        FILE *file=fopen(effective,"rb");if(!file)return VT_IPTV_NOT_FOUND;
        if(fseek(file,0,SEEK_END)){fclose(file);return VT_IPTV_IO;}long total=ftell(file);
        if(total<0){fclose(file);return VT_IPTV_IO;}
        uint64_t begin=ranged?offset:0;size_t n=ranged?limit:(size_t)total;
        if((!ranged&&(uint64_t)total>limit)||begin>(uint64_t)total||n>(uint64_t)total-begin){fclose(file);return ranged?VT_IPTV_RANGE:VT_IPTV_LIMIT;}
        if(begin>LONG_MAX||fseek(file,(long)begin,SEEK_SET)){fclose(file);return VT_IPTV_RANGE;}
        *data=malloc(n?n:1);if(!*data){fclose(file);return VT_IPTV_MEMORY;}
        *size=fread(*data,1,n,file);int failed=ferror(file);fclose(file);
        if(cancel&&*cancel)return VT_IPTV_CANCELLED;
        return failed||*size!=n?VT_IPTV_IO:0;
    }
    char checked[VT_IPTV_RESOURCE_URL_MAX];
    if (vt_iptv_resolve_url(url, url, checked, sizeof(checked))) {
        IPTV_TRACE("IPTV request rejected: invalid URL length=%u\n",url?(unsigned)strlen(url):0);
        return VT_IPTV_INVALID;
    }
    if (cancel && *cancel) return VT_IPTV_CANCELLED;
    VitaHttpsClientConfig config = {.user_agent = "VitaMediaDeck/1.4.2", .allow_http = 1,
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
    VitaHttpsRequest request = {.method = "GET", .url = checked, .write = receive,
        .write_opaque = &response, .cancel_flag = cancel};
    RangeHeader range={0};char range_text[96],agent_header[VT_IPTV_USER_AGENT_MAX+16];
    const char *headers[3]={0};unsigned header_count=0;
    if(ranged) {
        if(offset>UINT64_MAX-limit){vita_https_client_destroy(client);return VT_IPTV_INVALID;}
        snprintf(range_text,sizeof(range_text),"Range: bytes=%" PRIu64 "-%" PRIu64,offset,offset+limit-1);
        headers[header_count++]=range_text;request.header=range_header;request.header_opaque=&range;
    }
    if(options && options->user_agent[0]) {
        /* vita-https stores its default agent in 128 bytes. Override that
         * header explicitly so validated broadcaster agents up to 255 bytes
         * reach the server intact; never accept arbitrary playlist headers. */
        snprintf(agent_header,sizeof(agent_header),"User-Agent: %s",options->user_agent);
        headers[header_count++]=agent_header;
    }
    request.headers=headers;
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
    if (response.error) return metadata.status_code>=400?network_error(result,metadata.status_code):response.error;
    if(ranged && (metadata.status_code==416 || (result>=0 && metadata.status_code==200)))return VT_IPTV_RANGE;
    if (result < 0 || metadata.status_code != (ranged?206:200)) return network_error(result,metadata.status_code);
    if(ranged && (!range.valid || range.first!=offset || range.last!=offset+limit-1 || response.size!=limit))return VT_IPTV_RANGE;
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
