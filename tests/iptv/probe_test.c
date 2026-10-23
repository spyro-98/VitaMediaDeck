#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <vita_https.h>
#include "network/iptv_probe.h"
static long status;
static int transport,body,cancel_during,creates,destroys;
static const char *effective="https://example.org/channel.m3u8";
VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *config) {
    assert(config->allow_http && config->connect_timeout_ms==3000 && config->request_timeout_ms==5000);
    assert(!config->allow_untrusted_ca_with_pin && !config->password);
    creates++;return (void *)1;
}
void vita_https_client_destroy(VitaHttpsClient *client){assert(client);destroys++;}
int vita_https_perform(VitaHttpsClient *client,const VitaHttpsRequest *request,VitaHttpsResponse *response) {
    assert(client && !strcmp(request->method,"GET") && !request->body && !request->headers);
    response->status_code=status;
    snprintf(response->effective_url,sizeof(response->effective_url),"%s",effective);
    if(cancel_during)*request->cancel_flag=1;
    if(body)assert(request->write("discarded",9,request->write_opaque)==0);
    return transport;
}
static int probe(void){volatile int cancel=0;return vt_iptv_http_probe(NULL,"https://example.org/channel.m3u8",&cancel);}
int main(void) {
    status=200;body=1;transport=VITA_HTTPS_ERROR_CURL_BASE-CURLE_WRITE_ERROR;
    assert(probe()==VT_IPTV_PROBE_REACHABLE);
    status=403;assert(probe()==VT_IPTV_PROBE_403);
    status=404;assert(probe()==VT_IPTV_PROBE_404);
    const int others[]={401,410,429,500,302};
    for(unsigned i=0;i<sizeof(others)/sizeof(*others);i++){status=others[i];assert(probe()==VT_IPTV_PROBE_UNKNOWN);}
    body=0;status=204;transport=0;assert(probe()==VT_IPTV_PROBE_REACHABLE);
    status=403;transport=VITA_HTTPS_ERROR_HTTP_STATUS;assert(probe()==VT_IPTV_PROBE_403);
    status=404;assert(probe()==VT_IPTV_PROBE_404);
    status=200;transport=VITA_HTTPS_ERROR_CURL_BASE-CURLE_OPERATION_TIMEDOUT;assert(probe()==VT_IPTV_PROBE_UNKNOWN);
    transport=VITA_HTTPS_ERROR_CURL_BASE-CURLE_PEER_FAILED_VERIFICATION;assert(probe()==VT_IPTV_PROBE_UNKNOWN);
    transport=VITA_HTTPS_ERROR_CURL_BASE-CURLE_TOO_MANY_REDIRECTS;assert(probe()==VT_IPTV_PROBE_UNKNOWN);
    transport=VITA_HTTPS_ERROR_CURL_BASE-CURLE_WRITE_ERROR;assert(probe()==VT_IPTV_PROBE_UNKNOWN);
    transport=VITA_HTTPS_ERROR_CURL_BASE-CURLE_COULDNT_RESOLVE_HOST;assert(probe()==VT_IPTV_PROBE_DNS);
    cancel_during=1;assert(probe()==VT_IPTV_PROBE_UNKNOWN);cancel_during=0;
    transport=0;effective="http://example.org/downgrade";assert(probe()==VT_IPTV_PROBE_UNKNOWN);
    volatile int cancel=1;int count=creates;
    assert(vt_iptv_http_probe(NULL,effective,&cancel)==VT_IPTV_PROBE_UNKNOWN && count==creates);
    cancel=0;VtIptvHttpOptions options={0};strcpy(options.user_agent,"x\r\nInjected: yes");
    assert(vt_iptv_http_probe(&options,effective,&cancel)==VT_IPTV_PROBE_UNKNOWN && count==creates);
    assert(vt_iptv_http_probe(NULL,"file:///secret",&cancel)==VT_IPTV_PROBE_UNKNOWN && count==creates);
    assert(creates==destroys);
    for(int i=0;i<=VT_IPTV_PROBE_404;i++)assert(vt_iptv_probe_hidden(i)==(i>=VT_IPTV_PROBE_DNS));
    puts("IPTV availability transport: classification, cancellation and bounded body checks passed");
}
