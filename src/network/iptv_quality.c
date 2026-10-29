#include "network/iptv_quality.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#ifdef __vita__
#include <psp2/io/fcntl.h>
#else
#include <unistd.h>
#endif
#define CACHE "ux0:data/VitaMediaDeck/iptv-quality.json"
#define TEMP CACHE ".tmp"
#define BACKUP CACHE ".bak"
static const char *str(json_t *o,const char *key){const char *s=json_string_value(json_object_get(o,key));return s?s:"";}
static json_t *load(void){
 FILE *f=fopen(CACHE,"rb");if(!f)f=fopen(BACKUP,"rb");if(!f)return json_array();
 if(fseek(f,0,SEEK_END)){fclose(f);return json_array();}long size=ftell(f);rewind(f);
 json_t *root=size>0&&size<=256*1024?json_loadf(f,JSON_REJECT_DUPLICATES,NULL):NULL;fclose(f);
 if(!json_is_array(root)||json_array_size(root)>128){json_decref(root);return json_array();}return root;
}
int vt_iptv_quality_cached(const char *url,const char *signature,unsigned context,VtIptvPreference *out){
 memset(out,0,sizeof(*out));char channel[17];vt_iptv_choice_key(url,channel);json_t *items=load();if(!items)return 0;
 time_t now=time(NULL);size_t i;json_t *item;int found=0;
 json_array_foreach(items,i,item){
  json_int_t at=json_integer_value(json_object_get(item,"at"));
  if(strcmp(str(item,"channel"),channel)||strcmp(str(item,"variants"),signature)||
     json_integer_value(json_object_get(item,"context"))!=context||now<at||now-at>172800)continue;
  const char *video=str(item,"video");if(strlen(video)!=16)continue;
  snprintf(out->video_key,sizeof(out->video_key),"%s",video);snprintf(out->audio_key,sizeof(out->audio_key),"%.16s",str(item,"audio"));
  out->width=json_integer_value(json_object_get(item,"width"));out->height=json_integer_value(json_object_get(item,"height"));
  out->bandwidth=json_integer_value(json_object_get(item,"bandwidth"));found=1;break;
 }
 json_decref(items);return found;
}
int vt_iptv_quality_save(const char *url,const char *signature,unsigned context,const VtIptvPreference *p,const VtIptvQualityReport *report){
 if(!p->video_key[0]||!report->valid)return -1;
 char channel[17];vt_iptv_choice_key(url,channel);json_t *items=load();if(!items)return -1;
 for(size_t i=0;i<json_array_size(items);)if(!strcmp(str(json_array_get(items,i),"channel"),channel))json_array_remove(items,i);else i++;
 if(json_array_size(items)>=128)json_array_remove(items,0);
 json_t *item=json_pack("{s:s,s:s,s:i,s:I,s:s,s:s,s:i,s:i,s:I,s:f,s:i}",
  "channel",channel,"variants",signature,"context",(int)context,"at",(json_int_t)time(NULL),
  "video",p->video_key,"audio",p->audio_key,"width",(int)p->width,"height",(int)p->height,
  "bandwidth",(json_int_t)p->bandwidth,"cost",vt_iptv_quality_cost(report),"hardware",report->hardware);
 int ok=item&&json_array_append_new(items,item)==0;FILE *f=ok?fopen(TEMP,"wb"):NULL;
 if(f){ok=!json_dumpf(items,f,JSON_COMPACT);if(fflush(f))ok=0;if(fclose(f))ok=0;}else ok=0;
 json_decref(items);if(!ok){remove(TEMP);return -1;}
 remove(BACKUP);int old=rename(CACHE,BACKUP)==0;
 if(rename(TEMP,CACHE)){if(old)rename(BACKUP,CACHE);remove(TEMP);return -1;}
 remove(BACKUP);return 0;
}

/* Per-channel choices are separate from favourites and the expiring benchmark
 * cache. Only a URL hash is stored; tokens/credentials never become JSON text. */
#define PLAYBACK "ux0:data/VitaMediaDeck/iptv-playback.json"
#define PLAYBACK_MAX 4096
static json_t *playback_read(const char *path,int *exists){
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 if(exists)*exists=1;
 if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);rewind(f);
 json_t *data=n>0&&n<=4*1024*1024?json_loadf(f,JSON_REJECT_DUPLICATES,NULL):NULL;fclose(f);
 if(!json_is_array(data)||json_array_size(data)>PLAYBACK_MAX){json_decref(data);return NULL;}
 size_t i;json_t *v;
 json_array_foreach(data,i,v)if(!json_is_object(v)||strlen(str(v,"channel"))!=16){json_decref(data);return NULL;}
 return data;
}
static json_t *playback_load_file(void){
 int exists=0;json_t *data=playback_read(PLAYBACK,&exists);
 if(!data)data=playback_read(PLAYBACK ".bak",&exists);
 return data?data:exists?NULL:json_array();
}
int vt_iptv_playback_load_channel(const char *url,const char *identity,VtIptvPreference *out){
 memset(out,0,sizeof(*out));char key[17];vt_iptv_choice_key(url,key);if(!key[0])return 0;
 json_t *items=playback_load_file();if(!items)return 0;size_t i;json_t *v;int found=0;
 /* Prefer stable identity before falling back to the URL alias. */
 json_t *match=NULL;int by_identity=0;
 if(identity&&*identity)json_array_foreach(items,i,v)if(!strcmp(str(v,"identity"),identity)){match=v;by_identity=1;break;}
 if(!match)json_array_foreach(items,i,v)if(!strcmp(str(v,"channel"),key)){match=v;break;}
 if(match){v=match;
  snprintf(out->video_key,sizeof(out->video_key),"%.16s",str(v,"video"));snprintf(out->audio_key,sizeof(out->audio_key),"%.16s",str(v,"audio"));
  snprintf(out->audio_language,sizeof(out->audio_language),"%s",str(v,"language"));snprintf(out->audio_name,sizeof(out->audio_name),"%s",str(v,"audio_name"));
  json_int_t w=json_integer_value(json_object_get(v,"width")),h=json_integer_value(json_object_get(v,"height")),b=json_integer_value(json_object_get(v,"bandwidth"));
  out->width=w>0&&w<=16384?w:0;out->height=h>0&&h<=16384?h:0;out->bandwidth=b>0&&b<=UINT32_MAX?b:0;
  found=by_identity?(!strcmp(str(v,"channel"),key)?1:3):2;
 }
 json_decref(items);return found;
}
static int playback_write(json_t *items){
 mkdir("ux0:data/VitaMediaDeck",0777);
#ifdef __vita__
 char *bytes=json_dumps(items,JSON_COMPACT);if(!bytes)return 0;
 size_t n=strlen(bytes),at=0;SceUID fd=sceIoOpen(PLAYBACK ".tmp",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);int ok=fd>=0;
 while(ok&&at<n){int wrote=sceIoWrite(fd,bytes+at,n-at);if(wrote<=0)ok=0;else at+=wrote;}
 if(ok&&sceIoSyncByFd(fd,0)<0)ok=0;
 if(fd>=0&&sceIoClose(fd)<0)ok=0;
 free(bytes);return ok;
#else
 FILE *f=fopen(PLAYBACK ".tmp","wb");int ok=0;
 if(f){ok=!json_dumpf(items,f,JSON_COMPACT);if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;}
 return ok;
#endif
}
static int playback_store(json_t *items){
 int ok=playback_write(items);json_decref(items);
 if(!ok){remove(PLAYBACK ".tmp");return -1;}
 int exists=0;json_t *primary=playback_read(PLAYBACK,&exists);int old=0;
 if(primary){
  json_decref(primary);remove(PLAYBACK ".bak");
  if(rename(PLAYBACK,PLAYBACK ".bak")){remove(PLAYBACK ".tmp");return -1;}
  old=1;
 }else if(exists&&remove(PLAYBACK)){remove(PLAYBACK ".tmp");return -1;}
 if(rename(PLAYBACK ".tmp",PLAYBACK)){if(old)rename(PLAYBACK ".bak",PLAYBACK);remove(PLAYBACK ".tmp");return -1;}
 /* Keep the preceding valid file as a recovery point. */
#ifdef __vita__
 if(sceIoSync("ux0:",0)<0)return -1;
#endif
 return 0;
}
int vt_iptv_playback_save_channel(const char *url,const char *identity,const VtIptvPreference *p){
 if(!p||(!p->video_key[0]&&!p->audio_key[0]&&!p->height&&!p->audio_language[0]))return -1;
 char key[17];vt_iptv_choice_key(url,key);if(!key[0])return -1;
 json_t *items=playback_load_file();if(!items)return -1;
 for(size_t i=0;i<json_array_size(items);){json_t *v=json_array_get(items,i);
  if(!strcmp(str(v,"channel"),key)||(identity&&*identity&&!strcmp(str(v,"identity"),identity)))json_array_remove(items,i);else i++;
 }
 if(json_array_size(items)>=PLAYBACK_MAX){json_decref(items);return -1;}
 json_t *entry=json_pack("{s:s,s:s,s:s,s:s,s:s,s:i,s:i,s:I}","channel",key,"video",p->video_key,"audio",p->audio_key,
  "language",p->audio_language,"audio_name",p->audio_name,"width",(int)p->width,"height",(int)p->height,"bandwidth",(json_int_t)p->bandwidth);
 if(entry&&identity&&*identity&&json_object_set_new(entry,"identity",json_string(identity))){json_decref(entry);entry=NULL;}
 if(!entry||json_array_append_new(items,entry)){json_decref(items);return -1;}
 return playback_store(items);
}
int vt_iptv_playback_remove_channel(const char *url,const char *identity){
 char key[17];vt_iptv_choice_key(url,key);if(!key[0])return -1;
 json_t *items=playback_load_file();if(!items)return -1;int changed=0;
 for(size_t i=0;i<json_array_size(items);){json_t *v=json_array_get(items,i);
  if(!strcmp(str(v,"channel"),key)||(identity&&*identity&&!strcmp(str(v,"identity"),identity))){json_array_remove(items,i);changed=1;}else i++;
 }
 if(!changed){json_decref(items);return 0;}
 return playback_store(items);
}

int vt_iptv_playback_load(const char *url,VtIptvPreference *out){return !!vt_iptv_playback_load_channel(url,NULL,out);}
int vt_iptv_playback_save(const char *url,const VtIptvPreference *p){return vt_iptv_playback_save_channel(url,NULL,p);}

