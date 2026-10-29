#include "network/iptv_probe.h"
#include "network/iptv_catalog.h"
#include <vita_https.h>
#include <curl/curl.h>
static size_t first_bytes(const void *data,size_t size,void *ctx) {
    (void)data;if(size)*(int*)ctx=1;return 0;
}
int vt_iptv_http_probe(const VtIptvHttpOptions *options,const char *url,volatile int *cancel) {
    char checked[VT_IPTV_URL_MAX];
    if(!cancel || __atomic_load_n(cancel,__ATOMIC_ACQUIRE) ||
       vt_iptv_resolve_url(url,url,checked,sizeof(checked)) ||
       (options && !vt_iptv_user_agent_valid(options->user_agent)))return VT_IPTV_PROBE_UNKNOWN;
    VitaHttpsClientConfig config={.user_agent=options&&options->user_agent[0]?options->user_agent:"VitaMediaDeck/1.4.3",
        .allow_http=1,.connect_timeout_ms=3000,.request_timeout_ms=5000};
    VitaHttpsClient *client=vita_https_client_create(&config);if(!client)return VT_IPTV_PROBE_UNKNOWN;
    int saw_body=0;
    VitaHttpsRequest request={.method="GET",.url=url,.write=first_bytes,.write_opaque=&saw_body,.cancel_flag=cancel};
    VitaHttpsResponse response={0};int r=vita_https_perform(client,&request,&response);
    vita_https_client_destroy(client);
    if(__atomic_load_n(cancel,__ATOMIC_ACQUIRE))return VT_IPTV_PROBE_UNKNOWN;
    if(r==VITA_HTTPS_ERROR_CURL_BASE-CURLE_COULDNT_RESOLVE_HOST)return VT_IPTV_PROBE_DNS;
    if(vt_iptv_resolve_url(url,response.effective_url,checked,sizeof(checked)))return VT_IPTV_PROBE_UNKNOWN;
    /* Only an intentional body abort or a completed HTTP exchange establishes
     * availability. A TLS/redirect/transport failure cannot masquerade as 200. */
    int completed=r>=0 || r==VITA_HTTPS_ERROR_HTTP_STATUS ||
        (saw_body && r==VITA_HTTPS_ERROR_CURL_BASE-CURLE_WRITE_ERROR);
    if(!completed)return VT_IPTV_PROBE_UNKNOWN;
    if(response.status_code==403)return VT_IPTV_PROBE_403;
    if(response.status_code==404)return VT_IPTV_PROBE_404;
    return response.status_code>=200 && response.status_code<300?VT_IPTV_PROBE_REACHABLE:VT_IPTV_PROBE_UNKNOWN;
}
