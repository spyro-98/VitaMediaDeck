#include "network/iptv_options.h"
#include "network/iptv_catalog.h"
#include "network/hls_source.h"
#include "network/dash_manifest.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <zlib.h>
#include <libxml/xmlreader.h>
#include <libavutil/parseutils.h>

static int fetch_body(const char *url,size_t limit,const VtIptvSourceConfig *cfg,
                      unsigned char **data,size_t *size,char effective[VT_IPTV_RESOURCE_URL_MAX],volatile int *cancel){
 VtIptvFetch fetch=cfg&&cfg->fetch?cfg->fetch:vt_iptv_http_fetch;
 *data=NULL;*size=0;effective[0]=0;
 int r=fetch(cfg?cfg->fetch_ctx:NULL,url,limit,data,size,effective,VT_IPTV_RESOURCE_URL_MAX,cancel);
 if(cancel&&__atomic_load_n(cancel,__ATOMIC_ACQUIRE))return VT_IPTV_CANCELLED;
 if(!r&&(!*data||!*size||*size>limit))r=VT_IPTV_LIMIT;
 char checked[VT_IPTV_RESOURCE_URL_MAX];
 if(!r)r=vt_iptv_resource_resolve(url,effective,checked,sizeof(checked));
 return r;
}
int vt_iptv_options_load(const char *url,int dash,const VtIptvSourceConfig *cfg,
                         VtIptvOptions *out,volatile int *cancel){
 if(!out)return VT_IPTV_INVALID;
 memset(out,0,sizeof(*out));unsigned char *data=NULL;size_t size=0;char effective[VT_IPTV_RESOURCE_URL_MAX];
 int r=fetch_body(url,VT_IPTV_CATALOG_LIMIT,cfg,&data,&size,effective,cancel);
 if(!r){
  VtIptvChannel identity={0};vt_iptv_catalog_metadata(data,size,effective,&identity);
  snprintf(out->channel_name,sizeof(out->channel_name),"%s",identity.title);
  snprintf(out->channel_logo,sizeof(out->channel_logo),"%s",identity.logo);
  snprintf(out->channel_id,sizeof(out->channel_id),"%s",identity.channel_id);
  int kind=vt_iptv_catalog_content_kind(data,size);out->content_kind=kind>=0?kind+1:0;
  if(kind==VT_IPTV_LIST){free(data);return VT_IPTV_OPTIONS_DIRECTORY;}
  if(kind==VT_IPTV_FILE){free(data);out->discovered=1;return 0;}
  if(kind==VT_IPTV_DASH)dash=1;
  if(size>VT_IPTV_PLAYLIST_LIMIT){free(data);return VT_IPTV_LIMIT;}
  VtIptvSourceConfig config=cfg?*cfg:(VtIptvSourceConfig){0};config.inspect=out;
  config.bandwidth_limit=config.width_limit=config.height_limit=0;
  if(dash){VtDashManifest *m=NULL;r=vt_dash_manifest_parse(data,size,effective,(int64_t)time(NULL)*1000000,&config,&m);vt_dash_manifest_free(m);}
  else {unsigned char *prepared=NULL;size_t length=0;r=vt_hls_prepare_manifest(data,size,effective,&config,&prepared,&length);free(prepared);}
 }
 free(data);if(!r)out->discovered=1;return r;
}
static void property(xmlNode *node,const char *name,char *out,size_t capacity){
 xmlChar *v=xmlGetProp(node,BAD_CAST name);out[0]=0;
 if(v){if((size_t)xmlStrlen(v)<capacity)snprintf(out,capacity,"%s",v);xmlFree(v);}
}
static void text(xmlNode *node,const char *name,const char *language,char *out,size_t cap){
 int best=-1;out[0]=0;
 for(xmlNode *n=node->children;n;n=n->next)if(n->type==XML_ELEMENT_NODE&&!xmlStrcmp(n->name,BAD_CAST name)){
  char lang[64];property(n,"lang",lang,sizeof(lang));
  int rank=language&&*language&&!strcmp(lang,language)?3:language&&strlen(language)>=2&&!strncmp(lang,language,2)&&(lang[2]=='\0'||lang[2]=='_'||lang[2]=='-')?2:!lang[0]?1:0;
  if(rank<=best)continue;
  xmlChar *v=xmlNodeGetContent(n);if(v){snprintf(out,cap,"%s",v);xmlFree(v);best=rank;}
 }
 /* UI text may wrap, but does not inherit control codes from a guide. */
 for(char *p=out;*p;p++)if((unsigned char)*p<32&&*p!='\n'&&*p!='\t')*p=' ';
}
/* XMLTV is separate from the stream. Parse compressed guides incrementally:
 * a large expanded country guide never needs a second full-size allocation. */
typedef struct {
 const unsigned char *bytes;size_t size,position,expanded;int gzip,ended,error;
 volatile int *cancel;z_stream z;char tail[7];unsigned tail_size;
} GuideInput;
static int guide_read(void *opaque,char *buffer,int capacity){
 GuideInput *in=opaque;
 if(in->error)return -1;
 if(in->cancel&&__atomic_load_n(in->cancel,__ATOMIC_ACQUIRE)){in->error=VT_IPTV_CANCELLED;return -1;}
 int count=0;
 if(in->gzip){
  if(in->ended)return 0;
  in->z.next_out=(unsigned char*)buffer;in->z.avail_out=capacity;
  int r=inflate(&in->z,Z_NO_FLUSH);count=capacity-(int)in->z.avail_out;
  if(r==Z_STREAM_END){in->ended=1;if(in->z.avail_in)in->error=VT_IPTV_INVALID;}
  else if(r!=Z_OK||!count)in->error=VT_IPTV_INVALID;
 }else{
  size_t n=in->size-in->position;if(n>(size_t)capacity)n=capacity;
  memcpy(buffer,in->bytes+in->position,n);in->position+=n;count=(int)n;
 }
 in->expanded+=count;
 if(in->expanded>VT_IPTV_GUIDE_EXPANDED_LIMIT)in->error=VT_IPTV_LIMIT;
 for(int i=0;i<count&&!in->error;i++){
  if(in->tail_size==7&&!memcmp(in->tail,"<!ENTIT",7)&&buffer[i]=='Y')in->error=VT_IPTV_INVALID;
  if(in->tail_size<7)in->tail[in->tail_size++]=buffer[i];
  else{memmove(in->tail,in->tail+1,6);in->tail[6]=buffer[i];}
 }
 return in->error?-1:count;
}
static xmlTextReaderPtr guide_reader(GuideInput *in,const void *data,size_t size,volatile int *cancel){
 memset(in,0,sizeof(*in));in->bytes=data;in->size=size;in->cancel=cancel;
 in->gzip=size>2&&in->bytes[0]==0x1f&&in->bytes[1]==0x8b;
 if(in->gzip){if(inflateInit2(&in->z,15+16)!=Z_OK)return NULL;in->z.next_in=(void*)data;in->z.avail_in=(uInt)size;}
 xmlTextReaderPtr r=xmlReaderForIO(guide_read,NULL,in,NULL,NULL,XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING|XML_PARSE_COMPACT);
 if(!r&&in->gzip)inflateEnd(&in->z);
 return r;
}
static int guide_close(xmlTextReaderPtr reader,GuideInput *in,int r,int root){
 int result=in->error?in->error:r<0||!root?VT_IPTV_INVALID:0;
 xmlFreeTextReader(reader);if(in->gzip)inflateEnd(&in->z);return result;
}
static int guide_node(xmlTextReaderPtr r,unsigned *nodes){
 if(++*nodes>2000000||xmlTextReaderDepth(r)>24||xmlTextReaderNodeType(r)==XML_READER_TYPE_ENTITY_REFERENCE)return VT_IPTV_LIMIT;
 return 0;
}
static int64_t guide_time(const char *value){
 size_t digits=strspn(value,"0123456789");if(digits!=8&&digits!=10&&digits!=12&&digits!=14)return -1;
 char stamp[15]="00000101000000";memcpy(stamp,value,digits);
 const char *zone=value+digits;while(isspace((unsigned char)*zone))zone++;
 if(!*zone||!strcmp(zone,"Z")||!strcmp(zone,"UTC")||!strcmp(zone,"GMT"))zone="Z";
 else{
  if((*zone!='+'&&*zone!='-')||strlen(zone)!=5)return -1;
  for(int i=1;i<5;i++)if(!isdigit((unsigned char)zone[i]))return -1;
  if((zone[1]-'0')*10+zone[2]-'0'>23||(zone[3]-'0')*10+zone[4]-'0'>59)return -1;
 }
 char iso[64];snprintf(iso,sizeof(iso),"%.4s-%.2s-%.2sT%.2s:%.2s:%.2s%s",stamp,stamp+4,stamp+6,stamp+8,stamp+10,stamp+12,zone);
 int64_t us;if(av_parse_time(&us,iso,0)<0)return -1;return us/1000000;
}
static void guide_key(const char *value,int display,char out[256]){
 size_t n=strlen(value),used=0;
 /* Country suffix is retained for ID comparisons, removed only for names.
  * '+' and region words are preserved: a +1/regional channel is not its parent. */
 if(display&&n>3&&value[n-3]=='.'&&isalpha((unsigned char)value[n-2])&&isalpha((unsigned char)value[n-1]))n-=3;
 for(size_t i=0;i<n&&used<255;i++){
  unsigned char c=value[i];if(display&&(c=='('||c=='['))break;
  if(isalnum(c)||c>=128||c=='+')out[used++]=c<128?(char)tolower(c):(char)c;
 }
 out[used]=0;
}
static int guide_name_equal(const char *a,const char *b){
 char ka[256],kb[256];guide_key(a,1,ka);guide_key(b,1,kb);return ka[0]&&!strcmp(ka,kb);
}
/* Exporters may prefix the display name with its logical channel number,
 * while XMLTV names omit it (27 Twentyseven / Twenty Seven). This is a lower
 * priority, unique-name comparison; IDs, countries and +1 are left intact. */
static void guide_display_alias(const char *value,char out[256]){
 while(isspace((unsigned char)*value))value++;
 const char *p=value;while(isdigit((unsigned char)*p))p++;
 if(p>value&&(*p==' '||*p=='\t'||*p=='.'||*p=='-'||*p==':'||*p=='|')){
  while(isspace((unsigned char)*p)||*p=='.'||*p=='-'||*p==':'||*p=='|')p++;
  if(isalpha((unsigned char)*p))value=p;
 }
 char name[256];snprintf(name,sizeof(name),"%s",value);name[strcspn(name,"([")]=0;
 size_t n=strlen(name);while(n&&isspace((unsigned char)name[n-1]))name[--n]=0;
 const char *suffixes[]={" HD"," SD"," FHD"," UHD"," 4K"};
 for(unsigned i=0;i<sizeof(suffixes)/sizeof(*suffixes);i++){
  size_t length=strlen(suffixes[i]);if(n>length&&!strcasecmp(name+n-length,suffixes[i])){name[n-length]=0;break;}
 }
 guide_key(name,1,out);
}
static int guide_alias_equal(const char *a,const char *b){
 char ka[256],kb[256];guide_display_alias(a,ka);guide_display_alias(b,kb);return ka[0]&&!strcmp(ka,kb);
}
static int guide_display_rank(const VtIptvGuideQuery *q,const char *value){
 if(q->name&&guide_name_equal(q->name,value))return 80;
 if(q->title&&guide_name_equal(q->title,value))return 70;
 if(q->name&&guide_alias_equal(q->name,value))return 60;
 if(q->title&&guide_alias_equal(q->title,value))return 50;
 return 0;
}
static int guide_id_equal(const char *a,const char *b){
 char ka[256],kb[256];guide_key(a,0,ka);guide_key(b,0,kb);return ka[0]&&!strcmp(ka,kb);
}
static int guide_rank(const VtIptvGuideQuery *q,const char *id,xmlNode *node){
 const char *wanted=q->channel?q->channel:"";
 if(*wanted&&!strcmp(wanted,id))return 100;
 if(guide_id_equal(wanted,id))return 90;
 size_t a=strlen(wanted),b=strlen(id);
 if(a>3&&b>3&&wanted[a-3]=='.'&&id[b-3]=='.'&&strcasecmp(wanted+a-2,id+b-2))return 0;
 int rank=guide_display_rank(q,id);
 for(xmlNode *n=node?node->children:NULL;n;n=n->next)if(n->type==XML_ELEMENT_NODE&&!xmlStrcmp(n->name,BAD_CAST "display-name")){
  xmlChar *v=xmlNodeGetContent(n);if(v){
   int score=guide_display_rank(q,(char*)v);
   if(score>rank)rank=score;
   xmlFree(v);
  }
 }
 return rank;
}
static int guide_channel(const void *data,size_t size,const VtIptvGuideQuery *q,char id[128],const char *language,VtIptvProgram *identity,volatile int *cancel){
 GuideInput in;xmlTextReaderPtr reader=guide_reader(&in,data,size,cancel);if(!reader)return VT_IPTV_INVALID;
 int r,root=0,best=0,ambiguous=0,result=0;unsigned nodes=0;id[0]=0;
 while((r=xmlTextReaderRead(reader))==1){
  if((result=guide_node(reader,&nodes)))break;
  if(xmlTextReaderNodeType(reader)!=XML_READER_TYPE_ELEMENT)continue;
  const xmlChar *name=xmlTextReaderConstLocalName(reader);
  if(xmlTextReaderDepth(reader)==0){root=!xmlStrcmp(name,BAD_CAST "tv");if(!root){result=VT_IPTV_INVALID;break;}}
  if(xmlTextReaderDepth(reader)!=1||xmlStrcmp(name,BAD_CAST "channel"))continue;
  xmlNode *node=xmlTextReaderExpand(reader);if(!node){result=VT_IPTV_INVALID;break;}
  char candidate[128];property(node,"id",candidate,sizeof(candidate));int rank=guide_rank(q,candidate,node);
  if(rank>best){
   best=rank;ambiguous=0;snprintf(id,128,"%s",candidate);
   snprintf(identity->channel_id,sizeof(identity->channel_id),"%s",candidate);
   text(node,"display-name",language,identity->channel_name,sizeof(identity->channel_name));identity->channel_logo[0]=0;
   for(xmlNode *icon=node->children;icon;icon=icon->next)if(icon->type==XML_ELEMENT_NODE&&!xmlStrcmp(icon->name,BAD_CAST "icon")){
    char value[512],resolved[512];property(icon,"src",value,sizeof(value));
    const char *origin=q->url&&*q->url?q->url:value;
    if(!vt_iptv_resolve_url(origin,value,resolved,sizeof(resolved))){snprintf(identity->channel_logo,sizeof(identity->channel_logo),"%s",resolved);break;}
   }
  }
  else if(rank&&rank==best&&strcmp(id,candidate))ambiguous=1;
 }
 int close=guide_close(reader,&in,r,root);if(result||close)return result?result:close;
 if(ambiguous){id[0]=0;identity->channel_name[0]=identity->channel_logo[0]=identity->channel_id[0]=0;return 0;}
 if(!best&&q->channel)snprintf(id,128,"%s",q->channel);
 return 0;
}
int vt_iptv_program_parse_channel(const void *data,size_t size,const VtIptvGuideQuery *q,int64_t now,
                                  const char *language,VtIptvProgram *out,volatile int *cancel){
 if(!out)return VT_IPTV_INVALID;
 memset(out,0,sizeof(*out));out->guide_status=VT_IPTV_GUIDE_NO_CHANNEL;
 if(!data||!size||size>VT_IPTV_GUIDE_TRANSFER_LIMIT||!q||q->shift_seconds< -50400||q->shift_seconds>50400)return VT_IPTV_INVALID;
 char id[128];int result=guide_channel(data,size,q,id,language,out,cancel);if(result)return result;
 if(!id[0])return 0;
 GuideInput in;xmlTextReaderPtr reader=guide_reader(&in,data,size,cancel);if(!reader)return VT_IPTV_INVALID;
 int r,root=0,found=0;unsigned nodes=0;int64_t current=-1;
 while((r=xmlTextReaderRead(reader))==1){
  if((result=guide_node(reader,&nodes)))break;
  if(xmlTextReaderNodeType(reader)!=XML_READER_TYPE_ELEMENT)continue;
  const xmlChar *name=xmlTextReaderConstLocalName(reader);
  if(xmlTextReaderDepth(reader)==0)root=!xmlStrcmp(name,BAD_CAST "tv");
  if(xmlTextReaderDepth(reader)!=1||xmlStrcmp(name,BAD_CAST "programme"))continue;
  xmlChar *cid=xmlTextReaderGetAttribute(reader,BAD_CAST "channel");
  int match=cid&&guide_id_equal((char*)cid,id);xmlFree(cid);if(!match)continue;
  xmlNode *node=xmlTextReaderExpand(reader);if(!node){result=VT_IPTV_INVALID;break;}found=1;
  char start[64],stop[64];property(node,"start",start,sizeof(start));property(node,"stop",stop,sizeof(stop));
  int64_t begin=guide_time(start),end=stop[0]?guide_time(stop):0;
  if(begin<0||end<0||(end&&end<=begin))continue;
  begin+=q->shift_seconds;if(end)end+=q->shift_seconds;
  if(begin<=now&&begin>current){
   current=begin;out->start=begin;out->end=end;text(node,"title",language,out->title,sizeof(out->title));
   text(node,"sub-title",language,out->subtitle,sizeof(out->subtitle));text(node,"desc",language,out->description,sizeof(out->description));
   text(node,"category",language,out->category,sizeof(out->category));text(node,"episode-num",language,out->episode,sizeof(out->episode));
  }else if(begin>now){
   unsigned i=0;while(i<out->upcoming_count&&out->upcoming[i].start<begin)i++;
   if(i<out->upcoming_count&&out->upcoming[i].start==begin)continue;
   if(i<VT_IPTV_UPCOMING_MAX){
    unsigned count=out->upcoming_count;if(count<VT_IPTV_UPCOMING_MAX)out->upcoming_count++;
    else count--;
    for(unsigned j=count;j>i;j--)out->upcoming[j]=out->upcoming[j-1];
    VtIptvUpcoming *p=&out->upcoming[i];memset(p,0,sizeof(*p));p->start=begin;p->end=end;
    text(node,"title",language,p->title,sizeof(p->title));text(node,"desc",language,p->description,sizeof(p->description));
   }
  }
 }
 int close=guide_close(reader,&in,r,root);if(result||close){memset(out,0,sizeof(*out));return result?result:close;}
 if(out->upcoming_count){
  for(unsigned i=0;i+1<out->upcoming_count;i++)if(!out->upcoming[i].end)out->upcoming[i].end=out->upcoming[i+1].start;
  VtIptvUpcoming *next=&out->upcoming[0];out->next_start=next->start;out->next_end=next->end;
  snprintf(out->next_title,sizeof(out->next_title),"%s",next->title);
  if(out->start&&!out->end)out->end=next->start;
 }
 out->available=out->title[0]&&out->start<=now&&out->end>now;
 out->guide_status=out->available||out->upcoming_count?VT_IPTV_GUIDE_OK:found?VT_IPTV_GUIDE_NO_PROGRAM:VT_IPTV_GUIDE_NO_CHANNEL;
 return 0;
}
int vt_iptv_program_parse(const void *data,size_t size,const char *channel,int64_t now,const char *language,VtIptvProgram *out){
 VtIptvGuideQuery q={.channel=channel};return vt_iptv_program_parse_channel(data,size,&q,now,language,out,NULL);
}
/* Four atomic raw-guide slots, at most 64 MiB in total. Hashes isolate providers
 * without persisting/logging their authenticated URLs. Only validated XML is cached. */
#define GUIDE_DIR "ux0:data/VitaMediaDeck/iptv-epg"
typedef struct {char magic[8],key[17];uint32_t length;int64_t fetched;} GuideCacheHeader;
static int guide_slot(const char *key){
 int chosen=0;int64_t oldest=INT64_MAX;
 for(int i=0;i<4;i++){
  char path[160];snprintf(path,sizeof(path),GUIDE_DIR "/%d.bin",i);
  FILE *f=fopen(path,"rb");GuideCacheHeader h;int valid=f&&fread(&h,1,sizeof(h),f)==sizeof(h)&&!memcmp(h.magic,"VMDEPG1",8);
  if(f)fclose(f);
  if(valid&&!memcmp(h.key,key,17))return i;
  if(!valid){chosen=i;oldest=INT64_MIN;}
  else if(h.fetched<oldest){oldest=h.fetched;chosen=i;}
 }
 return chosen;
}
static unsigned char *guide_cached(const char *url,size_t *size,int64_t *fetched){
 char key[17];vt_iptv_choice_key(url,key);
 for(int i=0;i<4;i++){
  char path[160];snprintf(path,sizeof(path),GUIDE_DIR "/%d.bin",i);
  FILE *f=fopen(path,"rb");if(!f){char backup[168];snprintf(backup,sizeof(backup),"%s.bak",path);f=fopen(backup,"rb");}if(!f)continue;
  GuideCacheHeader h;unsigned char *data=NULL;
  if(fread(&h,1,sizeof(h),f)==sizeof(h)&&!memcmp(h.magic,"VMDEPG1",8)&&!memcmp(h.key,key,17)&&h.length&&h.length<=VT_IPTV_GUIDE_TRANSFER_LIMIT){
   data=malloc(h.length);if(data&&(fread(data,1,h.length,f)!=h.length||fgetc(f)!=EOF)){free(data);data=NULL;}
   if(data){*size=h.length;*fetched=h.fetched;}
  }
  fclose(f);if(data)return data;
 }
 return NULL;
}
static void guide_cache(const char *url,const void *data,size_t size,int64_t now){
 mkdir("ux0:data",0777);mkdir("ux0:data/VitaMediaDeck",0777);mkdir(GUIDE_DIR,0777);
 GuideCacheHeader h={0};memcpy(h.magic,"VMDEPG1",8);vt_iptv_choice_key(url,h.key);h.length=(uint32_t)size;h.fetched=now;
 char path[160],tmp[168];snprintf(path,sizeof(path),GUIDE_DIR "/%u.bin",(unsigned)guide_slot(h.key));snprintf(tmp,sizeof(tmp),"%s.tmp",path);
 FILE *f=fopen(tmp,"wb");if(!f)return;
 int ok=fwrite(&h,1,sizeof(h),f)==sizeof(h)&&fwrite(data,1,size,f)==size;if(fflush(f))ok=0;if(fclose(f))ok=0;
 if(ok){
  char backup[168];snprintf(backup,sizeof(backup),"%s.bak",path);struct stat st;int old=!stat(path,&st);
  if(old)remove(backup);
  if(old&&rename(path,backup)){remove(tmp);return;}
  if(!rename(tmp,path)){remove(backup);return;}
  if(old)rename(backup,path);
 }
 remove(tmp);
}
static int program_load_one(const VtIptvGuideQuery *q,const VtIptvSourceConfig *config,const char *language,VtIptvProgram *out,volatile int *cancel){
 if(!out||!q)return VT_IPTV_INVALID;
 memset(out,0,sizeof(*out));out->guide_status=VT_IPTV_GUIDE_MISSING;
 VtIptvGuideQuery query=*q;const char *id=q->channel?q->channel:"";size_t n=strlen(id);
 /* A public Italian fallback is confined to IDs explicitly declaring .it.
  * No user's playlist, token or channel URL is sent to this public endpoint. */
 int public_source=0;
 if(!query.url||!*query.url){
  if(n>3&&!strcasecmp(id+n-3,".it")){query.url=VT_IPTV_PUBLIC_ITALY_GUIDE;public_source=1;}
  else return 0;
 }
 if(!strcmp(query.url,VT_IPTV_PUBLIC_ITALY_GUIDE))public_source=1;
 char checked[VT_IPTV_RESOURCE_URL_MAX];if(vt_iptv_resolve_url(query.url,query.url,checked,sizeof(checked)))return VT_IPTV_INVALID;
 int64_t now=time(NULL),fetched=0;size_t cached_size=0;
 unsigned char *cached=guide_cached(query.url,&cached_size,&fetched);
 int64_t age=now-fetched,ttl=public_source?86400:21600;
 if(cached&&age>=0&&age<(query.refresh?300:ttl)){
  int r=vt_iptv_program_parse_channel(cached,cached_size,&query,now,language,out,cancel);free(cached);
  if(!r){out->cached=1;out->public_source=public_source;return 0;}
  if(r==VT_IPTV_CANCELLED)return r;
  cached=NULL;
 }
 unsigned char *data=NULL;size_t size=0;char effective[VT_IPTV_RESOURCE_URL_MAX];
 int r=fetch_body(query.url,VT_IPTV_GUIDE_TRANSFER_LIMIT,config,&data,&size,effective,cancel);
 if(!r)r=vt_iptv_program_parse_channel(data,size,&query,now,language,out,cancel);
 if(!r){guide_cache(query.url,data,size,now);out->public_source=public_source;}
 free(data);
 if(r&&r!=VT_IPTV_CANCELLED&&cached){
  int fallback=vt_iptv_program_parse_channel(cached,cached_size,&query,now,language,out,cancel);
  if(!fallback){out->cached=out->stale=1;out->public_source=public_source;r=0;}else r=fallback;
 }
 free(cached);return r;
}
int vt_iptv_program_load_channel(const VtIptvGuideQuery *q,const VtIptvSourceConfig *config,const char *language,VtIptvProgram *out,volatile int *cancel){
 if(!q||!out)return VT_IPTV_INVALID;
 if(!q->url||!strchr(q->url,','))return program_load_one(q,config,language,out,cancel);
 const char *p=q->url;int result=0;unsigned count=0;
 VtIptvProgram best={0};best.guide_status=VT_IPTV_GUIDE_NO_CHANNEL;
 while(*p&&count++<4){
  const char *end=strchr(p,',');if(!end)end=p+strlen(p);
  char url[512];size_t n=end-p;if(n>=sizeof(url))return VT_IPTV_LIMIT;
  memcpy(url,p,n);url[n]=0;VtIptvGuideQuery one=*q;one.url=url;
  result=program_load_one(&one,config,language,out,cancel);
  if(result==VT_IPTV_CANCELLED||(!result&&out->available))return result;
  if(!result&&(out->upcoming_count||!best.upcoming_count))best=*out;
  p=*end?end+1:end;
 }
 if(best.upcoming_count||best.guide_status==VT_IPTV_GUIDE_NO_PROGRAM){*out=best;return 0;}
 return result;
}
int vt_iptv_program_load(const char *url,const char *channel,const VtIptvSourceConfig *config,const char *language,VtIptvProgram *out,volatile int *cancel){
 VtIptvGuideQuery q={.url=url,.channel=channel};return vt_iptv_program_load_channel(&q,config,language,out,cancel);
}
