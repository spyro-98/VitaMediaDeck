#include <assert.h>
#include <stdarg.h>
int sceKernelDelayThread(unsigned);
#include "../../src/network/jellyfin_source.c"
static unsigned checks,calls,deletes;static int mode,force_expected;static uint64_t position_expected;
#define CHECK(v) do{checks++;assert(v);}while(0)
void log_printf(const char *s,...){(void)s;}
VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *c){CHECK(c->request_timeout_ms>0);return (void*)1;}
void vita_https_client_destroy(VitaHttpsClient *c){CHECK(c==(void*)1);}
int vita_https_perform(VitaHttpsClient *c,const VitaHttpsRequest *r,VitaHttpsResponse *s){(void)c;(void)r;(void)s;CHECK(!"Authenticated playback must not follow redirects");return -1;}
int vita_https_perform_no_redirects(VitaHttpsClient *c,const VitaHttpsRequest *r,VitaHttpsResponse *s){
 CHECK(c==(void*)1);calls++;memset(s,0,sizeof(*s));s->status_code=200;
 CHECK(r->headers&&strstr(r->headers[0],"TEST_TOKEN"));
 if(r->cancel_flag&&*r->cancel_flag)return -1;
 if(!strcmp(r->method,"DELETE")){CHECK(strstr(r->url,"Videos/ActiveEncodings?")&&strstr(r->url,"PlaySessionId=session-1"));deletes++;s->status_code=204;return 0;}
 if(!strcmp(r->method,"POST")){
  CHECK(strstr(r->url,"/jellyfin/Items/item-1/PlaybackInfo?"));
  json_t *payload=json_loadb(r->body,r->body_size,0,NULL);CHECK(payload);
  CHECK(json_integer_value(json_object_get(payload,"StartTimeTicks"))==(json_int_t)position_expected*10000);
  CHECK(json_is_true(json_object_get(payload,"EnableDirectPlay"))==!force_expected);
  json_t *profile=json_object_get(payload,"DeviceProfile");CHECK(profile&&json_array_size(json_object_get(profile,"TranscodingProfiles"))==1);
  CHECK(!strcmp(playback_text(json_array_get(json_object_get(profile,"TranscodingProfiles"),0),"Protocol"),"hls"));json_decref(payload);
  const char *body=mode==0?"{\"PlaySessionId\":\"session-1\",\"MediaSources\":[{\"Id\":\"source-1\",\"SupportsDirectPlay\":true,\"RunTimeTicks\":72000000000}]}":
   mode==2?"{\"PlaySessionId\":\"session-1\",\"MediaSources\":[{\"Id\":\"source-1\",\"SupportsTranscoding\":true,\"TranscodingUrl\":\"https://other.example/master.m3u8\"}]}":
   mode==3?"{\"ErrorCode\":\"NoCompatibleStream\"}":
   "{\"PlaySessionId\":\"session-1\",\"MediaSources\":[{\"Id\":\"source-1\",\"SupportsTranscoding\":true,\"RunTimeTicks\":72000000000,\"TranscodingUrl\":\"/jellyfin/Videos/item-1/master.m3u8?api_key=TEST_TOKEN\"}]}";
  if(mode==4)s->status_code=403;
  return r->write(body,strlen(body),r->write_opaque)==strlen(body)?0:-1;
 }
 CHECK(!strcmp(r->method,"GET"));
 if(mode==5){s->status_code=302;return 0;}
 if(r->headers[1]){
  CHECK(!strcmp(r->headers[1],"Range: bytes=64-251"));s->status_code=206;
  const char *header=mode==6?"Content-Range: bytes 0-187/1024\r\n":"Content-Range: bytes 64-251/1024\r\n";
  CHECK(r->header&&r->header(header,strlen(header),r->header_opaque)==strlen(header));
 }
 unsigned char data[188];memset(data,0x47,sizeof(data));return r->write(data,sizeof(data),r->write_opaque)==sizeof(data)?0:-1;
}
int main(void){
 VtNetworkSource source={.protocol=VT_NETWORK_JELLYFIN,.host="https://media.example",.root_path="/jellyfin",.port=443};
 VtNetworkCredential credential={.access_token="TEST_TOKEN",.user_id="user-1"};volatile int cancel=0;
 VtJellyfinPlayback p;position_expected=12345;
 CHECK(!vt_jellyfin_playback_prepare(&source,&credential,"/item-1",position_expected,0,&p,&cancel));
 CHECK(!p.transcoding&&p.duration_ms==7200000&&!strcmp(p.media_source_id,"source-1"));
 CHECK(!vt_jellyfin_playback_stop(&source,&credential,&p,12345)&&!deletes);
 mode=1;force_expected=1;
 CHECK(!vt_jellyfin_playback_prepare(&source,&credential,"/item-1",position_expected,1,&p,&cancel));
 CHECK(p.transcoding&&p.position_ms==12345&&strstr(p.url,"https://media.example/jellyfin/Videos/"));
 VtJellyfinHlsTransport t={.source=&source,.credential=&credential};unsigned char *data=NULL;size_t size=0;char effective[2048];
 CHECK(!vt_jellyfin_hls_fetch(&t,p.url,1024,&data,&size,effective,sizeof(effective),&cancel)&&size==188);free(data);
 CHECK(!vt_jellyfin_hls_fetch_range(&t,p.url,64,188,&data,&size,effective,sizeof(effective),&cancel)&&size==188);free(data);
 mode=6;CHECK(vt_jellyfin_hls_fetch_range(&t,p.url,64,188,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_RANGE&&!data);
 unsigned before=calls;CHECK(vt_jellyfin_hls_fetch(&t,"https://other.example/segment.ts",1024,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_REDIRECT&&calls==before);
 mode=5;CHECK(vt_jellyfin_hls_fetch(&t,p.url,1024,&data,&size,effective,sizeof(effective),&cancel)==VT_IPTV_REDIRECT&&!data);
 CHECK(!vt_jellyfin_playback_stop(&source,&credential,&p,20000)&&deletes==1&&!p.session_id[0]);
 CHECK(!vt_jellyfin_playback_stop(&source,&credential,&p,20000)&&deletes==1);
 for(mode=2;mode<=4;mode++)CHECK(vt_jellyfin_playback_prepare(&source,&credential,"item-1",position_expected,1,&p,&cancel)<0);
 mode=1;cancel=1;CHECK(vt_jellyfin_playback_prepare(&source,&credential,"item-1",position_expected,1,&p,&cancel)<0);
 printf("Jellyfin playback: %u checks; negotiation, profile, direct play, HLS, exact byte ranges, origin/auth isolation, cancellation and idempotent server cleanup passed.\n",checks);
}
