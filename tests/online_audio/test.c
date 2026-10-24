#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <jansson.h>
#include <vita_https.h>
#include "network/online_audio.h"
#include "network/http_audio_stream.h"
#include "media/live_audio.h"
#include "system/background_audio.h"
#include <psp2/io/stat.h>

static unsigned checks, outputs, opened, released, leases, calls, chunk = 1;
void log_printf(const char *format, ...) { (void)format; }
#define CHECK(x) do { checks++; assert(x); } while(0)
static unsigned muted, pause_test, cancel_test;
static int disconnect_test;
static int icy_test;
static int recover_test,feed_cover_test;
static unsigned metadata_seen;
static void metadata(void *ctx,const char *station,const char *title) {
    (void)ctx;CHECK(!strcmp(station,"Station"));
    if(*title){CHECK(!strcmp(title,"Artist - Song"));metadata_seen++;}
}
static uint64_t tick, last_position;
static volatile int cancelled, paused;
static char *mp3; static size_t mp3_size;
static const char *fixture;
static char last_url[1024];
static int stream_closed, stream_opened;
static int fake_read(void *ctx,void *data,size_t size){CHECK(ctx==(void*)2);memset(data,42,size);return (int)size;}
static int64_t fake_seek(void *ctx,int64_t offset,int whence){CHECK(ctx==(void*)2 && whence==SEEK_SET);return offset;}
static void fake_close(void *ctx){CHECK(ctx==(void*)2);stream_closed++;}
int vita_https_open_range_stream(VitaHttpsClient *client,const char *url,volatile int *cancel,VitaHttpsStream *out) {
    CHECK(client==(void*)1 && !strcmp(url,"https://example.org/podcast.mp3"));
    stream_opened++;
    if(cancel&&__atomic_load_n(cancel,__ATOMIC_ACQUIRE))return -1;
    *out=(VitaHttpsStream){.opaque=(void*)2,.read=fake_read,.seek=fake_seek,.close=fake_close,.size=12345};return 0;
}
uint64_t sceKernelGetProcessTimeWide(void) { return tick += 1000; }
int sceKernelPowerTick(int mode) { (void)mode; return 0; }
int sceKernelDelayThread(unsigned t) { tick += t; if(pause_test&&paused)paused=0; return 0; }
int vt_audio_volume_percent(void) { return 125; }
int vt_background_audio_acquire(VtBackgroundAudioLease *l) { l->acquired=1; leases++; return 0; }
int vt_background_audio_release(VtBackgroundAudioLease *l) { CHECK(l->acquired); leases--; return 0; }
int sceAudioOutOpenPort(int t,int n,int r,int m) { CHECK(t==1 && n==1024 && r==44100 && m==1); opened++; return 5; }
int sceAudioOutReleasePort(int p) { CHECK(p==5); released++; return 0; }
int sceAudioOutSetVolume(int p,int flags,int *v) { CHECK(p==5 && flags==3 && v[0]==v[1]); if(!v[0])muted++; return 0; }
int sceAudioOutOutput(int p,const void *pcm) { CHECK(p==5 && pcm); outputs++; if(pause_test && outputs==10)paused=1; if(pause_test && outputs==20)paused=0; if(cancel_test && outputs==25)cancelled=1; return 0; }
static void update(void *ctx,unsigned rate,unsigned bitrate,uint64_t pos) {
    (void)ctx; CHECK((rate==44100 && bitrate==128)||(paused&&!rate&&!bitrate)); CHECK(pos>=last_position); last_position=pos;
}
VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *c) { CHECK(c->allow_http && c->connect_timeout_ms>0); return (void*)1; }
void vita_https_client_destroy(VitaHttpsClient *c) { CHECK(c==(void*)1); }
int vita_https_perform(VitaHttpsClient *c,const VitaHttpsRequest *r,VitaHttpsResponse *s) {
    (void)c; calls++; memset(s,0,sizeof(*s)); s->status_code=200;
    snprintf(last_url,sizeof(last_url),"%s",r->url);
    if(*r->cancel_flag)return -1;
    if(feed_cover_test) {
        const char *body=strstr(r->url,"feed.xml")?"<rss><channel><image><url>podcast.png</url></image></channel></rss>":"\x89PNG\r\n\x1a\n";
        return r->write(body,strlen(body),r->write_opaque)==strlen(body)?0:-1;
    }
    if(recover_test) {
        const char *body=strstr(r->url,"byuuid")?"[{\"favicon\":\"https://example.org/recovered.png\",\"tags\":\"news talk\"}]":"\x89PNG\r\n\x1a\n";
        return r->write(body,strlen(body),r->write_opaque)==strlen(body)?0:-1;
    }
    if(fixture) return r->write(fixture,strlen(fixture),r->write_opaque)==strlen(fixture)?0:-1;
    char *stream=mp3;size_t stream_size=mp3_size;
    if(icy_test) {
        CHECK(r->header && !strcmp(r->headers[0],"Icy-MetaData: 1"));
        const char *headers[]={"HTTP/1.1 302 Redirect\r\n","icy-metaint: 13\r\n","HTTP/1.1 200 OK\r\n","icy-name: Station\r\n","icy-metaint: 257\r\n"};
        for(unsigned h=0;h<5;h++)CHECK(r->header(headers[h],strlen(headers[h]),r->header_opaque)==strlen(headers[h]));
        stream=calloc(1,mp3_size+(mp3_size/257+1)*33);CHECK(stream);stream_size=0;
        for(size_t i=0;i<mp3_size;) {
            size_t n=mp3_size-i<257?mp3_size-i:257;memcpy(stream+stream_size,mp3+i,n);stream_size+=n;i+=n;
            if(n==257){int full=(i/257)%2;stream[stream_size++]=full?2:0;
                if(full){memcpy(stream+stream_size,"StreamTitle='Artist - Song';",28);stream_size+=32;}}
        }
    }
    for(size_t i=0;i<stream_size;) {
        size_t n=chunk; if(n>stream_size-i)n=stream_size-i;
        if(r->write(stream+i,n,r->write_opaque)!=n){if(icy_test)free(stream);return -1;}
        i+=n;
    }
    if(icy_test)free(stream);
    if(!disconnect_test)cancelled=1; return 0;
}
char *vita_https_escape(const char *v,size_t n) {
    char *out=malloc(n*3+1), *p=out; assert(out);
    for(size_t i=0;i<n;i++) {
        unsigned char c=v[i];
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') *p++=c;
        else { snprintf(p,4,"%%%02X",c);p+=3; }
    }
    *p=0;return out;
}
void vita_https_free(void *p) { free(p); }
int sceIoGetstat(const char *p,SceIoStat *s) { struct stat x; int r=stat(p,&x); if(!r)s->st_size=x.st_size; return r; }
int sceIoRemove(const char *p) { return unlink(p); }
static int saved_fail_commit;
int sceIoRename(const char *a,const char *b) {
    if(saved_fail_commit && strstr(a,"online-audio.tmp"))return -1;
    return rename(a,b);
}
static void feed_test(void) {
    VtOnlineQuery *q=calloc(1,sizeof(*q)); CHECK(q);
    const char *good="<rss><channel><item><title>Episode &amp; one</title><guid>g1</guid><enclosure url='one.mp3' type='audio/mpeg'/></item><item><enclosure url='one.mp3'/></item><item/><item><enclosure url='https://example.org/v.mp4' type='video/mp4'/></item><item><enclosure url='file:///etc/passwd'/></item></channel></rss>";
    CHECK(!vt_online_parse_feed(good,strlen(good),"https://example.org/feed.xml",q));
    CHECK(q->count==1 && !strcmp(q->items[0].title,"Episode & one"));
    CHECK(!strcmp(q->items[0].url,"https://example.org/one.mp3"));
    const char *details="<rss xmlns:itunes='http://www.itunes.com/dtds/podcast-1.0.dtd'><channel><itunes:author>Author</itunes:author><itunes:image href='https://example.org/cover.png'/><item><description>&lt;p&gt;Description&lt;/p&gt;</description><itunes:duration>1:02:03</itunes:duration><enclosure url='one.mp3'/></item><title>Late show title</title></channel></rss>";
    CHECK(!vt_online_parse_feed(details,strlen(details),"https://example.org/feed",q));
    CHECK(!strcmp(q->items[0].author,"Author"));CHECK(!strcmp(q->items[0].description,"Description"));
    CHECK(!strcmp(q->items[0].artwork_url,"https://example.org/cover.png"));
    CHECK(!strcmp(q->items[0].show_title,"Late show title")&&q->items[0].duration_ms==3723000);
    const char *durations[]={"42"," 02:03 ","1:02:03","1:99","1:2:3:4","-1","abc","99999999999999999"};
    const uint64_t expected[]={42000,123000,3723000,0,0,0,0,0};
    for(unsigned i=0;i<sizeof(expected)/sizeof(*expected);i++) {
        char feed[512];snprintf(feed,sizeof(feed),"<rss><channel><item><duration>%s</duration><enclosure url='one.mp3'/></item></channel></rss>",durations[i]);
        CHECK(!vt_online_parse_feed(feed,strlen(feed),"https://example.org/feed",q));
        CHECK(q->count==1&&q->items[0].duration_ms==expected[i]);
    }
    const char *bad[]={"broken","<feed/>","<!DOCTYPE rss [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><rss><channel><item><title>&x;</title></item></channel></rss>","<!DOCTYPE rss SYSTEM 'https://example.org/evil'><rss><channel/></rss>"};
    const char *dates[]={"Mon, 07 Sep 2026 23:30:00 -0700","7 sep 2026 12:00:00 GMT",
        "2026-09-07T00:10:00+1200","29 Feb 2024 00:00:00 GMT","29 Feb 2025",
        "31 Apr 2026","garbage","","01 Jan 2040","07 Sep 20260"};
    const char *calendar[]={"2026-09-07","2026-09-07","2026-09-07","2024-02-29",
        "","","","","2040-01-01",""};
    for(unsigned i=0;i<sizeof(dates)/sizeof(*dates);i++) {
        char feed[512];snprintf(feed,sizeof(feed),"<rss><channel><item><pubDate>%s</pubDate><enclosure url='one.mp3'/></item></channel></rss>",dates[i]);
        CHECK(!vt_online_parse_feed(feed,strlen(feed),"https://example.org/feed",q));
        CHECK(q->count==1&&!strcmp(q->items[0].published_date,calendar[i]));
    }
    const char *dc="<rss xmlns:dc='http://purl.org/dc/elements/1.1/'><channel><item><dc:date>2026-09-07</dc:date><enclosure url='one.mp3'/></item></channel></rss>";
    CHECK(!vt_online_parse_feed(dc,strlen(dc),"https://example.org/feed",q));
    CHECK(!strcmp(q->items[0].published_date,"2026-09-07"));
    for(unsigned i=0;i<4;i++) CHECK(vt_online_parse_feed(bad[i],strlen(bad[i]),"https://example.org/",q)<0);
    CHECK(vt_online_parse_feed("",VT_ONLINE_FEED_LIMIT+1,"https://example.org/",q)<0);
    char large[16000]="<rss><channel>";
    for(int i=0;i<90;i++){ char row[100]; snprintf(row,sizeof(row),"<item><enclosure url='https://example.org/%d.mp3'/></item>",i);strcat(large,row); }
    strcat(large,"</channel></rss>");
    CHECK(!vt_online_parse_feed(large,strlen(large),"https://example.org/",q) && q->count==64);
    CHECK(q->has_more);q->offset=64;
    CHECK(!vt_online_parse_feed(large,strlen(large),"https://example.org/",q)&&q->count==26&&!q->has_more);q->offset=0;
    char spaced[128],compact[128];
    vt_online_search_normalize("BBCWorld RadioRai",spaced,compact,sizeof(spaced));
    CHECK(!strcmp(spaced,"bbc world radio rai")&&!strcmp(compact,"bbcworldradiorai"));
    vt_online_search_normalize("BBC World Radio Rai",spaced,compact,sizeof(spaced));
    CHECK(!strcmp(compact,"bbcworldradiorai"));
    const char *urls[]={"file:///x","https://","http://user:pass@a/x","https://?x","https://:80/x","https://a/x\n","ftp://a/x","https://a/#x"};
    for(unsigned i=0;i<8;i++)CHECK(!vt_online_url_valid(urls[i]));
    CHECK(vt_online_url_valid("http://192.168.1.2:8000/live"));
    CHECK(vt_online_url_valid("https://example.org/a@b.mp3"));
    fixture="{\"results\":[{\"collectionName\":\"Podcast\",\"feedUrl\":\"https://example.org/rss\"}]}";
    q->kind=VT_ONLINE_FEED; strcpy(q->query,"test");
    CHECK(!vt_online_fetch_query(q) && q->count==1 && q->items[0].kind==VT_ONLINE_FEED);
    fixture=NULL; free(q);
}
static void large_feed_test(void) {
    VtOnlineQuery *q=calloc(1,sizeof(*q)); CHECK(q);
    size_t padding=2500000;
    char *body=calloc(1,padding+2048); CHECK(body);
    strcpy(body,"<rss><channel><!--"); size_t prefix=strlen(body);
    memset(body+prefix,'x',padding);
    strcpy(body+prefix+padding,"--><item><title>Episode</title><enclosure url='one.mp3'/></item><author>Late author</author><description>Late description</description><image><url>cover.jpg</url></image></channel></rss>");
    CHECK(!vt_online_parse_feed(body,strlen(body),"https://example.org/feed",q));
    CHECK(q->count==1 && !strcmp(q->items[0].author,"Late author"));
    CHECK(!strcmp(q->items[0].description,"Late description"));
    CHECK(!strcmp(q->items[0].artwork_url,"https://example.org/cover.jpg"));
    fixture=body; q->kind=VT_ONLINE_EPISODE; strcpy(q->query,"https://example.org/feed");
    CHECK(!vt_online_fetch_query(q) && q->count==1);
    q->kind=VT_ONLINE_SHOW; CHECK(vt_online_fetch_query(q)<0); /* JSON remains capped at 2 MiB. */
    q->cancel=1;CHECK(vt_online_parse_feed(body,strlen(body),"https://example.org/",q)<0&&q->count==0);
    q->cancel=0;volatile int parent_cancel=1;q->cancel_parent=&parent_cancel;
    CHECK(vt_online_parse_feed(body,strlen(body),"https://example.org/",q)<0&&q->count==0);
    fixture=NULL;free(body);free(q);
}
static void real_feed_test(const char *path) {
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));
    long size=ftell(file);CHECK(size>0 && size<=VT_ONLINE_FEED_LIMIT);rewind(file);
    char *body=malloc((size_t)size+1);CHECK(body);CHECK(fread(body,1,size,file)==(size_t)size);fclose(file);body[size]=0;
    VtOnlineQuery *q=calloc(1,sizeof(*q));CHECK(q);unsigned total=0;
    do {CHECK(!vt_online_parse_feed(body,size,"https://example.org/feed",q));
        CHECK(q->count>0 && q->count<=64);total+=q->count;q->offset+=64;
    } while(q->has_more);
    printf("Feed fixture: bytes=%ld episodes=%u\n",size,total);
    q->offset=0;q->kind=VT_ONLINE_EPISODE;strcpy(q->query,"https://example.org/feed");fixture=body;
    CHECK(!vt_online_fetch_query(q) && q->count>0);fixture=NULL;
    free(q);free(body);
}
static void saved_test(void) {
    mkdir("ux0:data",0700); mkdir("ux0:data/VitaMediaDeck",0700);
    VtOnlineItem item={.title="Station",.url="https://example.org/live",.kind=VT_ONLINE_RADIO,
        .author="Publisher",.description="Description",.artwork_url="https://example.org/cover.png"}, loaded[64];
    CHECK(!vt_online_save(&item)); CHECK(!vt_online_save(&item)); CHECK(vt_online_load_saved(loaded,64)==1);
    CHECK(!strcmp(loaded[0].title,item.title));
    CHECK(!strcmp(loaded[0].author,item.author)&&!strcmp(loaded[0].description,item.description));
    CHECK(!strcmp(loaded[0].artwork_url,item.artwork_url));
    fixture="\x89PNG\r\n\x1a\n";CHECK(!vt_online_fetch_cover(&item,&cancelled));CHECK(strstr(item.artwork_path,".png"));
    unsigned requests=calls;CHECK(!vt_online_fetch_cover(&item,&cancelled)&&requests==calls);fixture=NULL;
    VtOnlineItem feed={.kind=VT_ONLINE_FEED,.url="https://example.org/feed.xml"};
    feed_cover_test=1;CHECK(!vt_online_fetch_cover(&feed,&cancelled));feed_cover_test=0;
    CHECK(!strcmp(feed.artwork_url,"https://example.org/podcast.png")&&feed.artwork_path[0]);
    /* The same URL may be a radio and a feed: remove only the requested kind. */
    feed.kind=VT_ONLINE_FEED;strcpy(feed.url,item.url);strcpy(feed.title,"Other provider");
    CHECK(!vt_online_save(&feed));CHECK(vt_online_load_saved(loaded,64)==2);
    saved_fail_commit=1;CHECK(vt_online_remove(item.kind,item.url)<0);saved_fail_commit=0;
    CHECK(vt_online_load_saved(loaded,64)==2);
    CHECK(!vt_online_remove(item.kind,item.url));CHECK(vt_online_load_saved(loaded,64)==1);
    CHECK(loaded[0].kind==VT_ONLINE_FEED&&!strcmp(loaded[0].title,feed.title));
    CHECK(!vt_online_remove(item.kind,item.url)); /* Exact-match idempotency. */
    CHECK(vt_online_remove(VT_ONLINE_EPISODE,item.url)<0);
    CHECK(vt_online_remove(VT_ONLINE_FEED,"file:///private/local")<0);
    CHECK(!rename("ux0:data/VitaMediaDeck/online-audio.json","ux0:data/VitaMediaDeck/online-audio.bak"));
    saved_fail_commit=1;CHECK(vt_online_remove(feed.kind,feed.url)<0);saved_fail_commit=0;
    CHECK(vt_online_load_saved(loaded,64)==1); /* Backup survives a failed recovery commit. */
    CHECK(!vt_online_remove(feed.kind,feed.url));CHECK(vt_online_load_saved(loaded,64)==0);
    CHECK(!vt_online_save(&item));
    FILE *f=fopen("ux0:data/VitaMediaDeck/online-audio.json","wb"); CHECK(f); fputs("broken",f); fclose(f);
    CHECK(vt_online_remove(item.kind,item.url)<0);
    CHECK(vt_online_save(&item)<0); CHECK(vt_online_load_saved(loaded,64)<0);
    unlink("ux0:data/VitaMediaDeck/online-audio.json");
}
static void catalog_test(void) {
    VtOnlineQuery *q=calloc(1,sizeof(*q));CHECK(q);
    q->kind=VT_ONLINE_COUNTRY;
    fixture="[{\"name\":\"Italy\",\"iso_3166_1\":\"IT\"},{\"name\":\"Japan\",\"iso_3166_1\":\"JP\"},{\"name\":\"Invalid\",\"iso_3166_1\":\"123\"}]";
    CHECK(!vt_online_fetch_query(q));CHECK(q->count==2 && !q->has_more);
    CHECK(!strcmp(q->items[0].id,"IT") && q->items[0].kind==VT_ONLINE_COUNTRY);
    CHECK(strstr(last_url,"/countries/?"));
    q->kind=VT_ONLINE_RADIO;strcpy(q->country,"IT");strcpy(q->query,"a&b");q->offset=64;
    fixture="[{\"name\":\"Station\",\"codec\":\"MP3\",\"countrycode\":\"IT\",\"state\":\"Lazio\",\"url_resolved\":\"https://example.org/live\"},{\"name\":\"Wrong country\",\"codec\":\"MP3\",\"countrycode\":\"FR\",\"url\":\"https://example.org/fr\"},{\"name\":\"AAC\",\"codec\":\"AAC\",\"countrycode\":\"IT\",\"url\":\"https://example.org/aac\"},{\"name\":\"HLS\",\"codec\":\"MP3\",\"countrycode\":\"IT\",\"hls\":1,\"url\":\"https://example.org/hls\"}]";
    CHECK(!vt_online_fetch_query(q) && q->count==1);
    CHECK(strstr(last_url,"countrycode=IT") && strstr(last_url,"offset=16") && strstr(last_url,"state=a%26b"));
    CHECK(strstr(q->items[0].detail,"Lazio"));
    json_t *array=json_array();
    for(int i=0;i<200;i++) { char url[64];snprintf(url,sizeof(url),"https://example.org/%d",i);
        json_array_append_new(array,json_pack("{s:s,s:s,s:s,s:s}","collectionName","Discovery","feedUrl",url,"artistName","Publisher","primaryGenreName","Science")); }
    json_t *root=json_pack("{s:o}","results",array);char *json=json_dumps(root,0);fixture=json;
    q->kind=VT_ONLINE_FEED;strcpy(q->country,"JP");
    CHECK(!vt_online_fetch_query(q) && q->count==32 && q->has_more);
    CHECK(!strcmp(q->items[0].url,"https://example.org/32"));
    CHECK(strstr(q->items[0].detail,"Publisher") && strstr(q->items[0].detail,"Science"));
    CHECK(strstr(last_url,"country=JP") && strstr(last_url,"limit=200"));
    q->offset=384;
    CHECK(!vt_online_fetch_query(q) && q->count==8 && !q->has_more);
    q->cancel=1;CHECK(vt_online_fetch_query(q)<0);q->cancel=0;
    memcpy(q->country,"BAD",3);CHECK(vt_online_fetch_query(q)<0);
    free(json);json_decref(root);fixture=NULL;free(q);
}
static void browser_test(void) {
    VtOnlineQuery *q=calloc(1,sizeof(*q));CHECK(q);q->kind=VT_ONLINE_SHOW;strcpy(q->country,"IT");
    fixture="{\"feed\":{\"results\":[{\"id\":\"123\",\"name\":\"Chart podcast\",\"artistName\":\"Author\",\"url\":\"https://podcasts.apple.com/it/podcast/id123\",\"artworkUrl100\":\"https://example.org/art.png\",\"genres\":[{\"name\":\"Science\"}]}]}}";
    CHECK(!vt_online_fetch_query(q)&&q->count==1&&q->items[0].kind==VT_ONLINE_SHOW);
    CHECK(strstr(last_url,"/it/podcasts/top/100/podcasts.json"));
    CHECK(strstr(q->items[0].detail,"Science")&&!strcmp(q->items[0].author,"Author"));
    fixture="{\"results\":[{\"feedUrl\":\"https://example.org/chart.xml\"}]}";
    CHECK(!vt_online_resolve_show(&q->items[0],&q->cancel));CHECK(q->items[0].kind==VT_ONLINE_FEED);
    CHECK(!strcmp(q->items[0].url,"https://example.org/chart.xml"));
    CHECK(strstr(last_url,"lookup?id=123&entity=podcast"));
    q->items[0].kind=VT_ONLINE_SHOW;strcpy(q->items[0].id,"123&evil=1");CHECK(vt_online_resolve_show(&q->items[0],&q->cancel)<0);
    q->kind=VT_ONLINE_TAG;fixture="[{\"name\":\"jazz\"},{\"name\":\"ambient\"}]";
    CHECK(!vt_online_fetch_query(q)&&q->count==2&&q->items[0].kind==VT_ONLINE_TAG);
    CHECK(strstr(last_url,"/json/tags?")&&strstr(last_url,"order=stationcount"));
    q->kind=VT_ONLINE_RADIO;q->browse=1;q->search_field=1;strcpy(q->query,"jazz");fixture="[]";
    CHECK(!vt_online_fetch_query(q));CHECK(strstr(last_url,"tag=jazz")&&strstr(last_url,"order=clickcount&reverse=true"));
    VtOnlineItem radio={.kind=VT_ONLINE_RADIO,.url="https://example.org/radio",.id="986c5985-41d7-11ea-a95e-52543be04c81"};
    fixture=NULL;recover_test=1;CHECK(!vt_online_fetch_cover(&radio,&q->cancel));recover_test=0;
    CHECK(!strcmp(radio.artwork_url,"https://example.org/recovered.png")&&radio.artwork_path[0]);
    CHECK(!strcmp(radio.description,"news talk"));free(q);
}
int main(int argc,char **argv) {
    CHECK(argc>=2); FILE *f=fopen(argv[1],"rb");CHECK(f);fseek(f,0,SEEK_END);mp3_size=ftell(f);rewind(f);mp3=malloc(mp3_size);CHECK(fread(mp3,1,mp3_size,f)==mp3_size);fclose(f);
    feed_test(); large_feed_test(); saved_test(); catalog_test();browser_test();
    for(int i=2;i<argc;i++)real_feed_test(argv[i]);
    VtDecoderStreamFactory factory;VtDecoderStreamHandle handle;
    vt_http_audio_stream_factory("file:///bad",&factory);CHECK(factory.open(factory.opaque,&handle)<0);
    vt_http_audio_stream_factory("https://example.org/podcast.mp3",&factory);
    cancelled=1;CHECK(factory.open_cancelable(factory.opaque,&handle,&cancelled)<0);
    CHECK(!stream_opened && !stream_closed && !handle.opaque);cancelled=0;
    CHECK(!factory.open_cancelable(factory.opaque,&handle,&cancelled));CHECK(handle.size==12345);
    char bytes[8];CHECK(handle.read(handle.opaque,bytes,sizeof(bytes))==8&&bytes[0]==42);
    CHECK(handle.seek(handle.opaque,100,SEEK_SET)==100);handle.close(handle.opaque);CHECK(stream_closed==1);
    unsigned chunks[]={1,7,417,4096,16384,65536};
    for(unsigned i=0;i<6;i++) {
        chunk=chunks[i]; cancelled=paused=outputs=muted=calls=last_position=0;
        pause_test=i==3; cancel_test=i==4;
        VtLiveAudioConfig config={.url="https://example.org/live",.cancel=&cancelled,.paused=&paused,.update=update};
        vt_live_audio_run(&config);
        CHECK(calls==(pause_test?2:1) && leases==0 && opened==released);
        CHECK(cancel_test?outputs==25:outputs>=85);
        if(pause_test)CHECK(muted==0 && outputs==96);
        printf("MP3 chunk=%u: %u audio grains, paused grains=%u\n",chunk,outputs,muted);
    }
    for(unsigned i=0;i<6;i++) {
        chunk=chunks[i];icy_test=1;pause_test=cancel_test=0;cancelled=paused=outputs=calls=last_position=metadata_seen=0;
        VtLiveAudioConfig icy={.url="https://example.org/live",.cancel=&cancelled,.paused=&paused,.update=update,.metadata=metadata};
        vt_live_audio_run(&icy);CHECK(outputs==86&&metadata_seen>0&&calls==1&&opened==released&&leases==0);
    }
    icy_test=0;
    cancelled=paused=outputs=muted=calls=last_position=0;
    pause_test=cancel_test=0;disconnect_test=1;
    VtLiveAudioConfig config={.url="https://example.org/live",.cancel=&cancelled,.paused=&paused,.update=update};
    CHECK(vt_live_audio_run(&config)<0);
    CHECK(calls==3 && outputs==258 && leases==0 && opened==released);
    fixture="<html>Not an MP3 stream</html>";calls=outputs=0;
    CHECK(vt_live_audio_run(&config)<0 && calls==3 && outputs==0 && leases==0);
    CHECK(vt_live_audio_run(NULL)<0);
    free(mp3);printf("PASS: %u assertions\n",checks); return 0;
}
