/* Opt-in real HTTP -> production MP3 decoder smoke test. No audio is emitted
 * or saved. The mocked output port paces PCM and cancels after five seconds.
 * Link this and src/media/live_audio.c with host curl and mpg123. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <vita_https.h>
#include "media/live_audio.h"
#include "system/background_audio.h"
static volatile int cancel, paused;
static unsigned grains, rate, channels;
static uint64_t position;
uint64_t sceKernelGetProcessTimeWide(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000 + t.tv_nsec/1000;
}
int sceKernelPowerTick(int x) { (void)x; return 0; }
int sceKernelDelayThread(unsigned us) { return usleep(us); }
int vt_audio_volume_percent(void) { return 100; }
int vt_background_audio_acquire(VtBackgroundAudioLease *l) { l->acquired=1;return 0; }
int vt_background_audio_release(VtBackgroundAudioLease *l) { assert(l->acquired);return 0; }
int sceAudioOutOpenPort(int t,int n,int r,int m) { (void)t;(void)m;assert(n==1024 && r>0);rate=r;channels=2;return 1; }
int sceAudioOutReleasePort(int p) { assert(p==1);return 0; }
int sceAudioOutSetVolume(int p,int f,int *v) { (void)p;(void)f;(void)v;return 0; }
int sceAudioOutOutput(int p,const void *pcm) { assert(p==1 && pcm);grains++;usleep(1024ULL*1000000/rate);return 0; }
static void update(void *ctx,unsigned r,unsigned bitrate,uint64_t ms) {
    (void)ctx; position=ms;
    if(ms>=5000) { printf("Decoded live MP3: %u Hz, %u kbps, %u channels, %u PCM grains, %llu ms\n",r,bitrate,channels,grains,(unsigned long long)ms);cancel=1; }
}
VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *c) { (void)c;return (void*)1; }
void vita_https_client_destroy(VitaHttpsClient *c) { (void)c; }
static size_t write_body(char *p,size_t n,size_t m,void *ctx) { const VitaHttpsRequest *r=ctx;return r->write(p,n*m,r->write_opaque); }
static size_t write_header(char *p,size_t n,size_t m,void *ctx) {const VitaHttpsRequest *r=ctx;return r->header?r->header(p,n*m,r->header_opaque):n*m;}
static void metadata(void *ctx,const char *station,const char *title) {(void)ctx;printf("Metadata: station=%s; now=%s\n",station,title);}
static int progress(void *ctx,curl_off_t t,curl_off_t n,curl_off_t ut,curl_off_t un) {
    const VitaHttpsRequest *r=ctx;(void)ut;(void)un;return r->progress(n,t,r->progress_opaque);
}
int vita_https_perform(VitaHttpsClient *client,const VitaHttpsRequest *r,VitaHttpsResponse *s) {
    (void)client;CURL *c=curl_easy_init();if(!c)return -1;
    curl_easy_setopt(c,CURLOPT_URL,r->url);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"VitaMediaDeck/1.4.1-host-smoke");
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,!strncmp(r->url,"https://",8)?"https":"http,https");
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT_MS,10000L);
    curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,20000L);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,write_body);curl_easy_setopt(c,CURLOPT_WRITEDATA,r);
    curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,write_header);curl_easy_setopt(c,CURLOPT_HEADERDATA,r);
    curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(c,CURLOPT_XFERINFODATA,r);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
    struct curl_slist *headers=NULL;
    for(int i=0;r->headers && r->headers[i];i++)headers=curl_slist_append(headers,r->headers[i]);
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
    CURLcode code=curl_easy_perform(c);curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&s->status_code);
    curl_slist_free_all(headers);curl_easy_cleanup(c);return code==CURLE_OK?0:-1;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;curl_global_init(CURL_GLOBAL_DEFAULT);
    VtLiveAudioConfig config={argv[1],&cancel,&paused,update,NULL,NULL,metadata};
    vt_live_audio_run(&config);curl_global_cleanup();
    if(position<5000){fprintf(stderr,"No five-second live decode completed\n");return 1;}return 0;
}
