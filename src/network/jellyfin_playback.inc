#include "network/jellyfin_playback.h"
#include <inttypes.h>
static const char *playback_text(json_t *object,const char *key){
 const char *s=json_string_value(json_object_get(object,key));return s?s:"";
}
static int playback_id(const char *id){
 size_t n=id?strlen(id):0;return n>0&&n<64&&strspn(id,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==n;
}
static int playback_same_origin(const VtNetworkSource *source,const char *url){
 char base[2048],checked[4096];
 if(jellyfin_base_url(source,base,sizeof(base))<0||vt_iptv_resolve_url(base,url,checked,sizeof(checked)))return 0;
 const char *a=strstr(base,"://"),*b=strstr(checked,"://");if(!a||!b)return 0;
 size_t an=(size_t)(a+3-base)+strcspn(a+3,"/?#"),bn=(size_t)(b+3-checked)+strcspn(b+3,"/?#");
 return an==bn&&!strncasecmp(base,checked,an);
}
static const char jellyfin_vita_profile[]=
 "{\"Name\":\"PS Vita\",\"MaxStreamingBitrate\":4000000,"
 "\"DirectPlayProfiles\":[{\"Container\":\"mp4,m4v,mkv,mov,ts,mpegts\",\"Type\":\"Video\",\"VideoCodec\":\"h264\",\"AudioCodec\":\"aac,mp3\"}],"
 "\"TranscodingProfiles\":[{\"Container\":\"ts\",\"Type\":\"Video\",\"VideoCodec\":\"h264\",\"AudioCodec\":\"aac\",\"Protocol\":\"hls\",\"Context\":\"Streaming\",\"MaxAudioChannels\":\"2\",\"MinSegments\":2,\"SegmentLength\":4,\"CopyTimestamps\":false}],"
 "\"CodecProfiles\":[{\"Type\":\"Video\",\"Codec\":\"h264\",\"Conditions\":["
 "{\"Condition\":\"LessThanEqual\",\"Property\":\"Width\",\"Value\":\"1280\",\"IsRequired\":true},"
 "{\"Condition\":\"LessThanEqual\",\"Property\":\"Height\",\"Value\":\"720\",\"IsRequired\":true},"
 "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoBitDepth\",\"Value\":\"8\",\"IsRequired\":true},"
 "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoLevel\",\"Value\":\"41\",\"IsRequired\":false}]},"
 "{\"Type\":\"VideoAudio\",\"Codec\":\"aac,mp3\",\"Conditions\":[{\"Condition\":\"LessThanEqual\",\"Property\":\"AudioChannels\",\"Value\":\"2\",\"IsRequired\":true}]}]}";
int vt_jellyfin_playback_prepare(const VtNetworkSource *s,const VtNetworkCredential *c,const char *path,
 uint64_t position,int force,VtJellyfinPlayback *out,volatile int *cancel){
 if(!s||!c||!out||!c->access_token[0]||!playback_id(c->user_id)||position>INT64_MAX/10000)return VT_NETWORK_ERROR;
 memset(out,0,sizeof(*out));
 char endpoint[160],url[2048],auth[512];
 if(item_id_from_path(path,out->item_id)||!out->item_id[0])return VT_NETWORK_ERROR;
 snprintf(endpoint,sizeof(endpoint),"Items/%s/PlaybackInfo",out->item_id);
 if(jellyfin_url(s,endpoint,"IsPlayback=true&AutoOpenLiveStream=false",url,sizeof(url))||authorization_header(c->access_token,auth,sizeof(auth)))return VT_NETWORK_ERROR;
 json_t *profile=json_loads(jellyfin_vita_profile,0,NULL);
 if(!profile)return VT_NETWORK_ERROR;
 json_t *payload=json_pack("{s:s,s:I,s:i,s:i,s:b,s:b,s:b,s:b,s:b,s:o}",
  "UserId",c->user_id,"StartTimeTicks",(json_int_t)(position*10000),"MaxStreamingBitrate",4000000,
  "MaxAudioChannels",2,"EnableDirectPlay",!force,"EnableDirectStream",1,"EnableTranscoding",1,
  "AllowVideoStreamCopy",!force,"AllowAudioStreamCopy",!force,"DeviceProfile",profile);
 if(!payload)return VT_NETWORK_ERROR;
 char *body=json_dumps(payload,JSON_COMPACT);json_decref(payload);if(!body)return VT_NETWORK_ERROR;
 JellyfinBuffer response={.limit=JELLYFIN_RESPONSE_MAX};long status=0;
 int result=request_json_impl(s,url,"POST",auth,body,&response,&status,cancel,0);secure_zero(body,strlen(body));free(body);
 if(result<0||status<200||status>=300){free(response.data);return result<0?result:VT_NETWORK_ERROR;}
 json_t *root=json_loadb((char*)response.data,response.size,0,NULL);free(response.data);
 json_t *sources=json_object_get(root,"MediaSources"),*chosen=NULL;size_t i;json_t *entry;
 if(!force)json_array_foreach(sources,i,entry)if(json_is_true(json_object_get(entry,"SupportsDirectPlay"))){chosen=entry;break;}
 if(!chosen)json_array_foreach(sources,i,entry)
  if((json_is_true(json_object_get(entry,"SupportsTranscoding"))||json_is_true(json_object_get(entry,"SupportsDirectStream")))&&*playback_text(entry,"TranscodingUrl")){chosen=entry;break;}
 const char *sid=playback_text(root,"PlaySessionId"),*mid=playback_text(chosen,"Id");
 if(!chosen||!playback_id(sid)||!playback_id(mid)){json_decref(root);return VT_NETWORK_UNSUPPORTED_MEDIA;}
 snprintf(out->session_id,sizeof(out->session_id),"%s",sid);snprintf(out->media_source_id,sizeof(out->media_source_id),"%s",mid);
 json_int_t duration=json_integer_value(json_object_get(chosen,"RunTimeTicks"));
 out->duration_ms=duration>0?(uint64_t)duration/10000:0;out->position_ms=position;
 out->transcoding=force||!json_is_true(json_object_get(chosen,"SupportsDirectPlay"));
 if(out->transcoding){
  char base[2048];int n=jellyfin_base_url(s,base,sizeof(base));size_t length=strlen(base);
  if(n||length+2>sizeof(base)){json_decref(root);return VT_NETWORK_ERROR;}
  if(base[length-1]!='/'){base[length++]='/';base[length]=0;}
  const char *target=playback_text(chosen,"TranscodingUrl");
  if(vt_iptv_resolve_url(base,target,out->url,sizeof(out->url))||!playback_same_origin(s,out->url))result=VT_NETWORK_UNSUPPORTED_MEDIA;
  else {size_t end=strcspn(out->url,"?");if(end<5||strncasecmp(out->url+end-5,".m3u8",5))result=VT_NETWORK_UNSUPPORTED_MEDIA;}
 }
 json_decref(root);
 log_printf("jellyfin playback negotiation: server_stream=%d result=%d\n",out->transcoding,result);
 return result;
}
typedef struct {uint64_t start,end;int valid,seen;} JellyfinContentRange;
static size_t jellyfin_range_header(const void *bytes,size_t n,void *opaque){
 JellyfinContentRange *r=opaque;
 if(n>14&&!strncasecmp(bytes,"Content-Range:",14)){
  char line[192];if(n>=sizeof(line)||r->seen++){r->valid=0;return n;}
  memcpy(line,bytes,n);line[n]=0;uint64_t total=0;
  r->valid=sscanf(line+14," bytes %"SCNu64"-%"SCNu64"/%"SCNu64,&r->start,&r->end,&total)==3&&r->end>=r->start&&total>r->end;
 }
 return n;
}
static int jellyfin_hls_get(void *opaque,const char *url,uint64_t offset,size_t limit,int ranged,
 unsigned char **data,size_t *size,char *effective,size_t capacity,volatile int *cancel){
 VtJellyfinHlsTransport *t=opaque;
 if(!t||!data||!size||!effective||!capacity||!limit||limit>32u*1024u*1024u)return VT_IPTV_INVALID;
 *data=NULL;*size=0;effective[0]=0;
 if(!playback_same_origin(t->source,url))return VT_IPTV_REDIRECT;
 if(cancel&&__atomic_load_n(cancel,__ATOMIC_ACQUIRE))return VT_IPTV_CANCELLED;
 char auth[512],range[96];if(authorization_header(t->credential->access_token,auth,sizeof(auth)))return VT_IPTV_ACCESS;
 if(ranged&&offset>UINT64_MAX-limit)return VT_IPTV_RANGE;
 snprintf(range,sizeof(range),"Range: bytes=%"PRIu64"-%"PRIu64,offset,offset+limit-1);
 const char *headers[]={auth,ranged?range:NULL,NULL};
 VitaHttpsClientConfig config;jellyfin_client_config(t->source,&config);config.request_timeout_ms=20000;
 VitaHttpsClient *client=vita_https_client_create(&config);if(!client)return VT_IPTV_MEMORY;
 JellyfinBuffer response={.limit=limit};VitaHttpsResponse transport={0};
 JellyfinContentRange content_range={0};
 VitaHttpsRequest request={.method="GET",.url=url,.headers=headers,.write=buffer_write,.write_opaque=&response,.cancel_flag=cancel,
  .header=ranged?jellyfin_range_header:NULL,.header_opaque=&content_range};
 /* Authenticated segment redirects must never forward a token to another origin. */
 int r=vita_https_perform_no_redirects(client,&request,&transport);vita_https_client_destroy(client);
 if(cancel&&__atomic_load_n(cancel,__ATOMIC_ACQUIRE))r=VT_IPTV_CANCELLED;
 else if(transport.status_code==401||transport.status_code==403)r=VT_IPTV_ACCESS;
 else if(transport.status_code==404)r=VT_IPTV_NOT_FOUND;
 else if(transport.status_code>=300&&transport.status_code<400)r=VT_IPTV_REDIRECT;
 else if(r<0)r=VT_IPTV_IO;
 else if(transport.status_code!=(ranged?206:200))r=VT_IPTV_IO;
 else if(ranged&&(!content_range.valid||content_range.start!=offset||content_range.end-offset!=limit-1))r=VT_IPTV_RANGE;
 else if(!response.size||response.size>limit||(ranged&&response.size!=limit))r=VT_IPTV_LIMIT;
 if(!r&&snprintf(effective,capacity,"%s",url)>=(int)capacity)r=VT_IPTV_LIMIT;
 if(r){free(response.data);return r;}
 *data=response.data;*size=response.size;return 0;
}
int vt_jellyfin_hls_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective,size_t n,volatile int *cancel){
 return jellyfin_hls_get(ctx,url,0,limit,0,data,size,effective,n,cancel);
}
int vt_jellyfin_hls_fetch_range(void *ctx,const char *url,uint64_t offset,size_t limit,unsigned char **data,size_t *size,char *effective,size_t n,volatile int *cancel){
 return jellyfin_hls_get(ctx,url,offset,limit,1,data,size,effective,n,cancel);
}
int vt_jellyfin_playback_stop(const VtNetworkSource *s,const VtNetworkCredential *c,VtJellyfinPlayback *p,uint64_t position){
 if(!p||!playback_id(p->session_id))return 0;
 char url[2048],auth[512],query[192];
 if(authorization_header(c->access_token,auth,sizeof(auth)))return -1;
 snprintf(query,sizeof(query),"DeviceId="JELLYFIN_DEVICE_ID"&PlaySessionId=%s",p->session_id);
 if(jellyfin_url(s,"Videos/ActiveEncodings",query,url,sizeof(url)))return -1;
 VitaHttpsClientConfig config;jellyfin_client_config(s,&config);config.request_timeout_ms=3000;
 VitaHttpsClient *client=vita_https_client_create(&config);if(!client)return -1;
 const char *headers[]={auth,NULL};VitaHttpsResponse response={0};
 VitaHttpsRequest request={.method="DELETE",.url=url,.headers=headers};
 int result=0;
 if(p->transcoding){
  result=vita_https_perform_no_redirects(client,&request,&response);
  if(!result&&response.status_code!=404&&(response.status_code<200||response.status_code>=300))result=VT_NETWORK_ERROR;
 }
 vita_https_client_destroy(client);p->session_id[0]=0;(void)position;
 log_printf("jellyfin server stream stopped: result=%d\n",result);return result;
}
