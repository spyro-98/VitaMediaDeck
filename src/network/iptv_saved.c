#include "network/iptv_catalog.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define SAVED "ux0:data/VitaMediaDeck/iptv-saved.json"
#define BACKUP SAVED ".bak"
#define TEMP SAVED ".tmp"
static const char *string(json_t *entry, const char *key) {
    const char *text = json_string_value(json_object_get(entry,key)); return text ? text : "";
}
int vt_iptv_saved_load(VtIptvCatalog *out, int starred_only) {
    if (!out) return -1;
    memset(out,0,sizeof(*out)); struct stat st;
    const char *path = SAVED;
    if (stat(path,&st)) { path=BACKUP; if(stat(path,&st))return 0; }
    if (!S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>1024*1024) return -1;
    char *bytes=malloc(st.st_size); if(!bytes)return -1;
    FILE *f=fopen(path,"rb");size_t size=f?fread(bytes,1,st.st_size,f):0;if(f)fclose(f);
    json_t *root=size==(size_t)st.st_size?json_loadb(bytes,size,JSON_REJECT_DUPLICATES,NULL):NULL;free(bytes);
    json_t *entries=json_object_get(root,"items");
    if(json_integer_value(json_object_get(root,"version"))!=1 || !json_is_array(entries) || json_array_size(entries)>VT_IPTV_SAVED_MAX){json_decref(root);return -1;}
    size_t i;json_t *entry;
    json_array_foreach(entries,i,entry) {
        VtIptvChannel item={0};const char *url=string(entry,"url");
        item.mode=(int)json_integer_value(json_object_get(entry,"mode"));
        item.starred=json_is_true(json_object_get(entry,"starred"));
        if(vt_iptv_resolve_url(url,url,item.url,sizeof(item.url)) || item.mode<VT_IPTV_HLS || item.mode>VT_IPTV_DASH ||
           strlen(string(entry,"title"))>=sizeof(item.title) || strlen(string(entry,"logo"))>=sizeof(item.logo) ||
           !vt_iptv_user_agent_valid(string(entry,"user_agent"))) {json_decref(root);out->count=0;return -1;}
        snprintf(item.title,sizeof(item.title),"%s",string(entry,"title"));
        snprintf(item.user_agent,sizeof(item.user_agent),"%s",string(entry,"user_agent"));
        snprintf(item.group,sizeof(item.group),"%s",string(entry,"group"));
        snprintf(item.channel_id,sizeof(item.channel_id),"%s",string(entry,"channel_id"));
        const char *logo=string(entry,"logo");
        if(*logo)vt_iptv_resolve_url(logo,logo,item.logo,sizeof(item.logo));
        if(!starred_only || item.starred)out->items[out->count++]=item;
    }
    json_decref(root);return 0;
}
static int write_saved(const VtIptvCatalog *saved) {
    json_t *array=json_array(),*root=json_object();int ok=array&&root;
    for(unsigned i=0;ok&&i<saved->count;i++) {
        const VtIptvChannel *v=&saved->items[i];
        json_t *entry=json_pack("{s:s,s:s,s:s,s:i,s:b,s:s,s:s,s:s}","url",v->url,"title",v->title,"logo",v->logo,"mode",v->mode,"starred",!!v->starred,"user_agent",v->user_agent,"group",v->group,"channel_id",v->channel_id);
        if(!entry||json_array_append_new(array,entry))ok=0;
    }
    if(ok)ok=!json_object_set_new(root,"version",json_integer(1))&&!json_object_set(root,"items",array);
    FILE *f=ok?fopen(TEMP,"wb"):NULL;
    if(f){ok=json_dumpf(root,f,JSON_COMPACT)==0;if(fflush(f))ok=0;if(fclose(f))ok=0;}else ok=0;
    json_decref(array);json_decref(root);
    if(!ok){remove(TEMP);return -1;}
    struct stat st;int old=!stat(SAVED,&st);
    if(old && rename(SAVED,BACKUP)){remove(TEMP);return -1;}
    if(rename(TEMP,SAVED)){if(old)rename(BACKUP,SAVED);remove(TEMP);return -1;}
    remove(BACKUP);return 0;
}
int vt_iptv_saved_set(const VtIptvChannel *item) {
    if(!item || !vt_iptv_user_agent_valid(item->user_agent))return -1;
    char validated[VT_IPTV_URL_MAX];
    if(vt_iptv_resolve_url(item->url,item->url,validated,sizeof(validated)))return -1;
    VtIptvCatalog *saved=calloc(1,sizeof(*saved));if(!saved)return -1;
    if(vt_iptv_saved_load(saved,0)){free(saved);return -1;} /* Preserve corrupt storage. */
    unsigned index=0;while(index<saved->count&&strcmp(saved->items[index].url,item->url))index++;
    if(index==VT_IPTV_SAVED_MAX){free(saved);return -1;}
    if(index==saved->count)saved->count++;
    saved->items[index]=*item;
    int result=write_saved(saved);free(saved);return result;
}
int vt_iptv_saved_remove(const char *url) {
    char checked[VT_IPTV_URL_MAX];
    if(!url || vt_iptv_resolve_url(url,url,checked,sizeof(checked)))return -1;
    VtIptvCatalog *saved=calloc(1,sizeof(*saved));if(!saved)return -1;
    if(vt_iptv_saved_load(saved,0)){free(saved);return -1;}
    unsigned kept=0;
    for(unsigned i=0;i<saved->count;i++)
        if(strcmp(saved->items[i].url,url))saved->items[kept++]=saved->items[i];
    int result=0;
    if(kept!=saved->count){saved->count=kept;result=write_saved(saved);}
    free(saved);return result;
}
