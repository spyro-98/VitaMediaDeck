/* Actual XMLTV reader, M3U metadata, raw-guide cache and public fallback.
 * No live network or wall-clock fixture guesses. */
#include "network/iptv_options.h"
#include "network/iptv_catalog.h"
#include "network/hls_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>
#include <libavutil/parseutils.h>
static unsigned checks,fetches;
#define CHECK(x) do{checks++;assert(x);}while(0)
static const void *body;static size_t length;static int network_error;static char fetched_url[2048];
int vt_iptv_http_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective,size_t cap,volatile int *cancel){
 (void)ctx;fetches++;snprintf(fetched_url,sizeof(fetched_url),"%s",url);*data=NULL;*size=0;
 if(cancel&&*cancel)return VT_IPTV_CANCELLED;
 if(network_error)return VT_IPTV_TIMEOUT;
 if(strstr(url,"empty.test")){const char *empty="<tv/>";*size=strlen(empty);*data=(unsigned char*)strdup(empty);}
 else{CHECK(length<=limit);*data=malloc(length);CHECK(*data);memcpy(*data,body,length);*size=length;}
 snprintf(effective,cap,"%s",url);return 0;
}
static void stamp(int64_t seconds,char out[40]){time_t t=seconds;struct tm tm;gmtime_r(&t,&tm);strftime(out,40,"%Y%m%d%H%M%S +0000",&tm);}
static void cache_path(const char *url,char out[160]){
 char key[17];vt_iptv_choice_key(url,key);
 for(int i=0;i<4;i++){
  snprintf(out,160,"ux0:data/VitaMediaDeck/iptv-epg/%d.bin",i);FILE *f=fopen(out,"rb");if(!f)continue;
  char header[25];int ok=fread(header,1,25,f)==25&&!memcmp(header+8,key,17);fclose(f);if(ok)return;
 }
 CHECK(0);
}
static void age_cache(const char *url){
 char path[160];cache_path(url,path);FILE *f=fopen(path,"r+b");CHECK(f);
 /* Header is shared with the production reader; change only fetched time. */
 struct {char magic[8],key[17];uint32_t length;int64_t fetched;} h;
 CHECK(fread(&h,1,sizeof(h),f)==sizeof(h));h.fetched-=90000;rewind(f);CHECK(fwrite(&h,1,sizeof(h),f)==sizeof(h));CHECK(!fclose(f));
}
static unsigned char *compress_guide(const void *bytes,size_t size,size_t *out_size){
 size_t cap=compressBound(size)+64;unsigned char *out=malloc(cap);CHECK(out);z_stream z={0};
 CHECK(deflateInit2(&z,6,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)==Z_OK);z.next_in=(void*)bytes;z.avail_in=(uInt)size;z.next_out=out;z.avail_out=(uInt)cap;
 CHECK(deflate(&z,Z_FINISH)==Z_STREAM_END);*out_size=z.total_out;deflateEnd(&z);return out;
}
int main(int argc,char **argv){
 char work[]="/private/tmp/vmd-programme-test.XXXXXX";CHECK(mkdtemp(work)&&!chdir(work));
 int64_t us;CHECK(!av_parse_time(&us,"2026-10-28T12:30:00Z",0));int64_t now=us/1000000;
 const char *guide="<?xml version='1.0'?><!DOCTYPE tv SYSTEM 'https://invalid.test/never.dtd'><tv>"
 "<channel id='Rai 1.it'><display-name>Rai Uno</display-name><display-name>Rai 1</display-name><icon src='https://logos.example/rai1.png'/></channel>"
 "<channel id='Rai 1+1.it'><display-name>Rai 1+1</display-name></channel>"
 "<programme channel='Rai 1.it' start='202610281300 +0100'><title lang='en'>News</title><title lang='it-IT'>Telegiornale</title><desc lang='it'>Notizie &amp; scienza.</desc></programme>"
 "<programme channel='Rai 1.it' start='202610281600 +0100' stop='202610281700 +0100'><title>Terzo</title></programme>"
 "<programme channel='Rai 1.it' start='202610281400 +0100'><title>Secondo</title><desc>Approfondimenti.</desc></programme>"
 "<programme channel='Rai 1+1.it' start='202610281200 +0000' stop='202610281500 +0000'><title>Wrong timeshift</title></programme></tv>";
 VtIptvProgram p;volatile int cancel=0;VtIptvGuideQuery q={.channel="RAI1.IT",.title="Rai 1 (1080p)"};
 CHECK(!vt_iptv_program_parse_channel(guide,strlen(guide),&q,now,"it",&p,&cancel));
 CHECK(!strcmp(p.channel_name,"Rai Uno")&&!strcmp(p.channel_id,"Rai 1.it")&&!strcmp(p.channel_logo,"https://logos.example/rai1.png"));
 CHECK(p.available&&!strcmp(p.title,"Telegiornale")&&!strcmp(p.description,"Notizie & scienza."));
 CHECK(p.start==now-1800&&p.end==now+1800&&p.upcoming_count==2);
 CHECK(!strcmp(p.upcoming[0].title,"Secondo")&&p.upcoming[0].end==p.upcoming[1].start);
 CHECK(!strcmp(p.next_title,"Secondo")&&p.next_start==now+1800);
 q.channel="";q.name="Rai Uno";q.title="Some catalogue label";
 CHECK(!vt_iptv_program_parse_channel(guide,strlen(guide),&q,now,"it",&p,&cancel)&&p.available);
 q.channel="Rai1.es";q.name="Rai 1";
 CHECK(!vt_iptv_program_parse_channel(guide,strlen(guide),&q,now,"it",&p,&cancel)&&!p.available&&p.guide_status==VT_IPTV_GUIDE_NO_CHANNEL);
 q=(VtIptvGuideQuery){.channel="Rai1.it",.shift_seconds=3600};
 CHECK(!vt_iptv_program_parse_channel(guide,strlen(guide),&q,now+3600,"it",&p,&cancel)&&p.available&&p.start==now+1800);
 q.shift_seconds=0;
 const char *ambiguous="<tv><channel id='a'><display-name>News TV</display-name></channel><channel id='b'><display-name>News TV</display-name></channel><programme channel='a' start='20261028120000 +0000' stop='20261028130000 +0000'><title>Wrong</title></programme></tv>";
 VtIptvGuideQuery names={.title="News TV"};CHECK(!vt_iptv_program_parse_channel(ambiguous,strlen(ambiguous),&names,now,"it",&p,&cancel)&&!p.available);
 const char *ended="<tv><programme channel='a' start='20261028110000 +0000'><title>Old missing stop</title></programme><programme channel='a' start='20261028120000 +0000' stop='20261028121000 +0000'><title>Ended</title></programme><programme channel='a' start='20261028150000 +0000'><title>Future</title></programme></tv>";
 names=(VtIptvGuideQuery){.channel="a"};CHECK(!vt_iptv_program_parse_channel(ended,strlen(ended),&names,now,"it",&p,&cancel)&&!p.available&&p.upcoming_count==1);
 /* Missing stop may be inferred only from a following listing; stale title is not current. */
 const char *old="<tv><programme channel='a' start='20261027000000 +0000'><title>Old</title></programme></tv>";
 CHECK(!vt_iptv_program_parse_channel(old,strlen(old),&names,now,"it",&p,&cancel)&&!p.available&&p.guide_status==VT_IPTV_GUIDE_NO_PROGRAM);
 /* A real country guide can expand beyond the former 8 MiB bound without a full expanded allocation. */
 size_t huge_size=10*1024*1024,at=0;char *huge=malloc(huge_size+strlen(guide)+64);CHECK(huge);memcpy(huge,"<tv>",4);at=4;
 const char *padding="<channel id='unrelated'><display-name>Unrelated station</display-name></channel>";
 while(at<huge_size){memcpy(huge+at,padding,strlen(padding));at+=strlen(padding);}
 const char *matching=strstr(guide,"<channel id='Rai 1.it'>");memcpy(huge+at,matching,strlen(matching));at+=strlen(matching);
 size_t compressed_size;unsigned char *compressed=compress_guide(huge,at,&compressed_size);free(huge);
 CHECK(!vt_iptv_program_parse_channel(compressed,compressed_size,&q,now,"it",&p,&cancel)&&p.available);
 compressed[compressed_size-1]^=1;CHECK(vt_iptv_program_parse_channel(compressed,compressed_size,&q,now,"it",&p,&cancel)<0&&!p.available);free(compressed);
 const char *entity="<!DOCTYPE tv [<!ENTITY leak SYSTEM 'file:///etc/passwd'>]><tv><programme channel='a' start='20261028120000 +0000' stop='20261028130000 +0000'><title>&leak;</title></programme></tv>";
 compressed=compress_guide(entity,strlen(entity),&compressed_size);
 CHECK(vt_iptv_program_parse_channel(compressed,compressed_size,&names,now,"it",&p,&cancel)<0&&!p.available);free(compressed);
 cancel=1;CHECK(vt_iptv_program_parse_channel(guide,strlen(guide),&q,now,"it",&p,&cancel)==VT_IPTV_CANCELLED);cancel=0;
 /* Logical channel number + display spelling: the reported 27 TwentySeven. */
 const char *twenty="<tv><channel id='Twenty Seven.it'><display-name>Twenty Seven.it</display-name></channel><programme channel='Twenty Seven.it' start='20261028120000 +0000' stop='20261028130000 +0000'><title>Film</title><desc>Description</desc></programme></tv>";
 VtIptvGuideQuery twenty_query={.channel="27.Twentyseven.it",.title="27 Twentyseven"};
 CHECK(!vt_iptv_program_parse_channel(twenty,strlen(twenty),&twenty_query,now,"it",&p,&cancel)&&p.available&&!strcmp(p.description,"Description"));
 twenty_query.title="27 Twenty Seven HD (720p)";
 CHECK(!vt_iptv_program_parse_channel(twenty,strlen(twenty),&twenty_query,now,"it",&p,&cancel)&&p.available);
 twenty_query.title="27 Twenty Seven +1";twenty_query.channel="27.Twentyseven+1.it";
 CHECK(!vt_iptv_program_parse_channel(twenty,strlen(twenty),&twenty_query,now,"it",&p,&cancel)&&!p.available);
 twenty_query.title="27 Twenty Seven";twenty_query.channel="27.Twentyseven.es";
 CHECK(!vt_iptv_program_parse_channel(twenty,strlen(twenty),&twenty_query,now,"it",&p,&cancel)&&!p.available);
 /* On-disk cache survives calls and network failures; refresh validates before replacing it. */
 char xml[8192],a[40],b[40],c[40];int64_t wall=time(NULL);stamp(wall-600,a);stamp(wall+600,b);stamp(wall+1200,c);
 snprintf(xml,sizeof(xml),"<tv><channel id='Rai 1.it'><display-name>Rai 1</display-name></channel><programme channel='Rai 1.it' start='%s' stop='%s'><title>Current</title><desc>Real cached description</desc></programme><programme channel='Rai 1.it' start='%s' stop='%s'><title>Next</title></programme></tv>",a,b,b,c);
 body=xml;length=strlen(xml);q=(VtIptvGuideQuery){.url="https://epg.test/guide.xml.gz?private=never-logged",.channel="Rai1.it"};
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&!p.cached&&fetches==1);
 network_error=1;CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.cached&&!p.stale&&fetches==1);
 age_cache(q.url);q.refresh=1;CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.cached&&p.stale&&fetches==2);
 network_error=0;body="<broken";length=strlen(body);CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.stale);
 body=xml;length=strlen(xml);CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&!p.cached);
 /* Public fallback only for country-qualified Italian IDs; it sends no private channel URL. */
 unsigned before=fetches;q=(VtIptvGuideQuery){.channel="Rai1.es",.title="Rai 1"};CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.guide_status==VT_IPTV_GUIDE_MISSING&&fetches==before);
 q.channel="Rai1.it";CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.public_source&&!strcmp(fetched_url,VT_IPTV_PUBLIC_ITALY_GUIDE));
 before=fetches;CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.cached&&fetches==before);
 q.url="https://empty.test/guide.xml,https://epg-multi.test/guide.xml";CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available);
 /* Exporter attribute forms, provider EPG endpoint and time-shift preservation. */
 VtIptvCatalog *catalog=calloc(1,sizeof(*catalog));CHECK(catalog);
 const char *list="#EXTM3U tvg-url = 'guide.xml, next.xml.gz' tvg-shift='1.5'\n#EXTINF:-1 tvg-id = 'Rai1.it' tvg-name='Rai Uno' tvg-shift='-0.5',Rai 1 (1080p)\nlive.m3u8\n#EXTINF:-1 tvg-id=Rai2.it,Rai 2\ntwo.m3u8\n";
 CHECK(!vt_iptv_catalog_parse(list,strlen(list),"https://lists.test/path/tv.m3u",catalog)&&catalog->count==2);
 CHECK(!strcmp(catalog->items[0].guide_name,"Rai Uno")&&catalog->items[0].guide_shift== -1800&&catalog->items[1].guide_shift==5400);
 CHECK(!strcmp(catalog->items[0].guide_url,"https://lists.test/path/guide.xml,https://lists.test/path/next.xml.gz"));
 char provider[512];CHECK(vt_iptv_catalog_provider_guide("https://provider.test/sub/get.php?username=u%40x&password=p%26q&type=m3u_plus",provider));
 CHECK(!strcmp(provider,"https://provider.test/sub/xmltv.php?username=u%40x&password=p%26q"));
 CHECK(!vt_iptv_catalog_provider_guide("https://provider.test/unrelated?username=x&password=y",provider));free(catalog);
 if(argc>1){
  FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);void *data=malloc(n);CHECK(data&&fread(data,1,n,f)==(size_t)n);fclose(f);
  q=(VtIptvGuideQuery){.channel="Rai1.it"};CHECK(!vt_iptv_program_parse_channel(data,n,&q,wall,"it",&p,&cancel));
  CHECK(p.available&&p.description[0]&&p.end>wall&&p.upcoming_count>1);
  printf("Public Open-EPG fixture: current programme, description, end and %u upcoming listings matched Rai1.it.\n",p.upcoming_count);
  if(argc>2){
   FILE *list=fopen(argv[2],"rb");CHECK(list);CHECK(!fseek(list,0,SEEK_END));long bytes=ftell(list);rewind(list);char *m3u=malloc(bytes);CHECK(m3u&&fread(m3u,1,bytes,list)==(size_t)bytes);fclose(list);
   VtIptvCatalog *channels=calloc(1,sizeof(*channels));CHECK(channels);
   CHECK(!vt_iptv_catalog_parse(m3u,bytes,"https://fixture.test/list.m3u",channels));free(m3u);
   unsigned matched=0;
   for(unsigned i=0;i<8&&i<channels->count;i++){
    VtIptvChannel *channel=&channels->items[i];q=(VtIptvGuideQuery){.channel=channel->channel_id,.name=channel->guide_name,.title=channel->title};
    CHECK(!vt_iptv_program_parse_channel(data,n,&q,wall,"it",&p,&cancel));
    CHECK(p.available&&p.description[0]&&p.end>wall&&p.upcoming_count>0);matched++;
   }
   CHECK(matched==8);free(channels);
   printf("Previously fetched reported playlist + public guide: all 8 initial national channels match current description/end/upcoming data without any guide URL in the M3U.\n");
  }
  q=(VtIptvGuideQuery){.channel="27.Twentyseven.it",.title="27 TwentySeven"};
  CHECK(!vt_iptv_program_parse_channel(data,n,&q,wall,"it",&p,&cancel)&&p.available&&p.description[0]&&p.end>wall&&p.upcoming_count);
  puts("Reported 27 TwentySeven: matched actual Twenty Seven.it XMLTV entry with current description/end/upcoming data.");
  free(data);
 }
 printf("XMLTV programme integration: %u checks; aliases, unique names, country/+1 isolation, missing stop, six-entry schedule, gzip >8 MiB, cancellation, entities, persistent cache/failed refresh, public fallback, M3U quotes/shifts and provider discovery passed.\n",checks);
}
