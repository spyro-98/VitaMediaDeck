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
        json_int_t name_source=json_integer_value(json_object_get(entry,"name_source"));
        if(name_source>=VT_IPTV_NAME_UNKNOWN&&name_source<=VT_IPTV_NAME_CUSTOM)item.name_source=(int)name_source;
        item.single_channel=json_is_true(json_object_get(entry,"single_channel"));
        if(vt_iptv_resource_resolve(url,url,item.url,sizeof(item.url)) || item.mode<VT_IPTV_HLS || item.mode>VT_IPTV_DASH ||
           strlen(string(entry,"title"))>=sizeof(item.title) || strlen(string(entry,"logo"))>=sizeof(item.logo) ||
           !vt_iptv_user_agent_valid(string(entry,"user_agent"))) {json_decref(root);out->count=0;return -1;}
        snprintf(item.title,sizeof(item.title),"%s",string(entry,"title"));
        snprintf(item.user_agent,sizeof(item.user_agent),"%s",string(entry,"user_agent"));
        snprintf(item.group,sizeof(item.group),"%s",string(entry,"group"));
        snprintf(item.channel_id,sizeof(item.channel_id),"%s",string(entry,"channel_id"));
        snprintf(item.guide_name,sizeof(item.guide_name),"%s",string(entry,"guide_name"));
        json_int_t shift=json_integer_value(json_object_get(entry,"guide_shift"));
        if(shift>= -50400&&shift<=50400)item.guide_shift=(int)shift;
        const char *video=string(entry,"video_quality"),*audio=string(entry,"audio_quality");
        if(strlen(video)==16&&strspn(video,"0123456789abcdef")==16)snprintf(item.preferred.video_key,sizeof(item.preferred.video_key),"%s",video);
        if(strlen(audio)==16&&strspn(audio,"0123456789abcdef")==16)snprintf(item.preferred.audio_key,sizeof(item.preferred.audio_key),"%s",audio);
        json_int_t width=json_integer_value(json_object_get(entry,"quality_width")),height=json_integer_value(json_object_get(entry,"quality_height")),bandwidth=json_integer_value(json_object_get(entry,"quality_bandwidth"));
        if(width>0&&width<=16384)item.preferred.width=width;
        if(height>0&&height<=16384)item.preferred.height=height;
        if(bandwidth>0&&bandwidth<=UINT32_MAX)item.preferred.bandwidth=bandwidth;
        const char *guide=string(entry,"guide_url");
        if(*guide)vt_iptv_catalog_guide_reference(guide,guide,item.guide_url);
        const char *logo=string(entry,"logo");
        if(*logo)vt_iptv_resource_resolve(logo,logo,item.logo,sizeof(item.logo));
        if(!starred_only || item.starred)out->items[out->count++]=item;
    }
    json_decref(root);return 0;
}
static unsigned iptv_saved_revision_value;
unsigned vt_iptv_saved_revision(void){return __atomic_load_n(&iptv_saved_revision_value,__ATOMIC_ACQUIRE);}
static int write_saved(const VtIptvCatalog *saved) {
    json_t *array=json_array(),*root=json_object();int ok=array&&root;
    for(unsigned i=0;ok&&i<saved->count;i++) {
        const VtIptvChannel *v=&saved->items[i];
        json_t *entry=json_pack("{s:s,s:s,s:s,s:i,s:b,s:s,s:s,s:s,s:s,s:s,s:i}","url",v->url,"title",v->title,"logo",v->logo,"mode",v->mode,"starred",!!v->starred,"user_agent",v->user_agent,"group",v->group,"channel_id",v->channel_id,"guide_url",v->guide_url,"guide_name",v->guide_name,"guide_shift",v->guide_shift);
        if(entry&&(json_object_set_new(entry,"name_source",json_integer(v->name_source))||
            json_object_set_new(entry,"single_channel",json_boolean(v->single_channel)))){json_decref(entry);entry=NULL;}
        if(entry&&(v->preferred.video_key[0]||v->preferred.height)){
            if(json_object_set_new(entry,"video_quality",json_string(v->preferred.video_key))||
               json_object_set_new(entry,"audio_quality",json_string(v->preferred.audio_key))||
               json_object_set_new(entry,"quality_width",json_integer(v->preferred.width))||
               json_object_set_new(entry,"quality_height",json_integer(v->preferred.height))||
               json_object_set_new(entry,"quality_bandwidth",json_integer(v->preferred.bandwidth))){json_decref(entry);entry=NULL;}
        }
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
    remove(BACKUP);
    __atomic_add_fetch(&iptv_saved_revision_value,1,__ATOMIC_RELEASE);return 0;
}
int vt_iptv_saved_set(const VtIptvChannel *item) {
    if(!item || !vt_iptv_user_agent_valid(item->user_agent))return -1;
    char validated[VT_IPTV_URL_MAX];
    if(vt_iptv_resource_resolve(item->url,item->url,validated,sizeof(validated)))return -1;
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
    if(!url || vt_iptv_resource_resolve(url,url,checked,sizeof(checked)))return -1;
    VtIptvCatalog *saved=calloc(1,sizeof(*saved));if(!saved)return -1;
    if(vt_iptv_saved_load(saved,0)){free(saved);return -1;}
    unsigned kept=0;
    for(unsigned i=0;i<saved->count;i++)
        if(strcmp(saved->items[i].url,url))saved->items[kept++]=saved->items[i];
    int result=0;
    if(kept!=saved->count){saved->count=kept;result=write_saved(saved);}
    free(saved);return result;
}
