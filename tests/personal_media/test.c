#include "network/personal_media.h"
#include <vita_https.h>
#include <openssl/md5.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks,requests,rng,fail_random,client_fail,transport_error;static long status=200;
static const char *fixture="{}",*expect_endpoint;static char last_url[4096],last_body[4096],last_header[300];
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
int mbedtls_md5(const unsigned char *in,size_t n,unsigned char out[16]){MD5(in,n,out);return 0;}
int sceKernelGetRandomNumber(void *p,size_t n){if(fail_random)return -1;memset(p,++rng,n);return 0;}
VitaHttpsClient *vita_https_client_create(const VitaHttpsClientConfig *c){CHECK(c->connect_timeout_ms>0&&c->connect_timeout_ms<=5000);CHECK(c->request_timeout_ms>0&&c->request_timeout_ms<=20000);return client_fail?NULL:(void*)1;}
void vita_https_client_destroy(VitaHttpsClient *c){CHECK(c==(void*)1);}
int vita_https_perform_no_redirects(VitaHttpsClient *c,const VitaHttpsRequest *r,VitaHttpsResponse *resp){CHECK(c==(void*)1);requests++;snprintf(last_url,sizeof(last_url),"%s",r->url);snprintf(last_body,sizeof(last_body),"%.*s",(int)r->body_size,r->body?(const char*)r->body:"");snprintf(last_header,sizeof(last_header),"%s",r->headers[0]);if(expect_endpoint)CHECK(strstr(r->url,expect_endpoint)!=NULL);resp->status_code=status;if(r->cancel_flag&&*r->cancel_flag)return -1;if(transport_error)return transport_error;CHECK(r->write(fixture,strlen(fixture),r->write_opaque)==strlen(fixture));return 0;}
static void auth_url(const char *password){const char *s=strstr(last_url,"&s="),*t=strstr(last_url,"&t=");CHECK(s&&t);char joined[512],salt[33],expected[33];memcpy(salt,s+3,32);salt[32]=0;snprintf(joined,sizeof(joined),"%s%s",password,salt);unsigned char digest[16];MD5((unsigned char*)joined,strlen(joined),digest);for(int i=0;i<16;i++)snprintf(expected+i*2,3,"%02x",digest[i]);CHECK(!strncmp(t+3,expected,32));CHECK(!strstr(last_url,password));CHECK(strstr(last_url,"u=user%20%26%2F%C3%A8")!=NULL);}
static int list(VtNetworkSource *s,VtNetworkCredential *c,const char *p,VtNetworkEntry *e,int cap){char detail[192];return vt_personal_list(s,c,p,e,cap,detail,sizeof(detail));}
int main(void){VtNetworkSource s={.protocol=VT_NETWORK_NAVIDROME};VtNetworkCredential c={0};snprintf(s.host,sizeof(s.host),"https://music.example/subpath/");snprintf(s.username,sizeof(s.username),"user &/è");snprintf(c.password,sizeof(c.password),"p&sswörd");char out[4096],detail[192];VtNetworkEntry e[256];
 CHECK(vt_personal_base_url(&s,out,sizeof(out))==0&&!strcmp(out,"https://music.example/subpath"));
 fixture="{\"subsonic-response\":{\"status\":\"ok\"}}";expect_endpoint="/subpath/rest/ping.view?";CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==0);auth_url(c.password);char old[4096];snprintf(old,sizeof(old),"%s",last_url);CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==0);CHECK(strcmp(old,last_url));
 CHECK(list(&s,&c,"",e,256)==3&&!strcmp(e[2].path,"playlists:0"));
 expect_endpoint="getAlbumList2";fixture="{\"subsonic-response\":{\"status\":\"ok\",\"albumList2\":{\"album\":[{\"id\":\"album-1\",\"name\":\"Album è\"},{\"id\":\"album-2\",\"name\":\"Two\"}]}}}";
 CHECK(list(&s,&c,"albums:0",e,256)==2&&!strcmp(e[0].path,"album:album-1"));CHECK(e[0].is_directory&&!strcmp(e[0].name,"Album è"));auth_url(c.password);
 expect_endpoint="getAlbum.view";fixture="{\"subsonic-response\":{\"status\":\"ok\",\"album\":{\"song\":[{\"id\":\"song-1\",\"title\":\"One\",\"duration\":235,\"size\":12345},{\"id\":\"evil&u=attacker\",\"title\":\"Unsafe ID\"}]}}}";
 CHECK(list(&s,&c,"album:album-1",e,256)==1&&e[0].is_audio&&!e[0].is_directory&&e[0].runtime_ms==235000&&e[0].size==12345);
 CHECK(vt_personal_stream_url(&s,&c,"song:song-1",out,sizeof(out))==0&&strstr(out,"format=raw"));CHECK(vt_personal_stream_url(&s,&c,"song:foo&x=y",out,sizeof(out))<0);CHECK(vt_personal_stream_url(&s,&c,"song:song-1",out,20)<0);
 expect_endpoint="search3";int before=requests;fixture="{\"subsonic-response\":{\"status\":\"ok\",\"searchResult3\":{\"artist\":[{\"id\":\"artist-1\",\"name\":\"Björk\"}]}}}";CHECK(list(&s,&c,"artists:0",e,256)==1&&!strcmp(e[0].path,"artist:artist-1"));CHECK(requests==before+1);CHECK(strstr(last_url,"albumCount=0&songCount=0"));
 expect_endpoint=NULL;fixture="{\"subsonic-response\":{\"status\":\"failed\",\"error\":{\"code\":40}}}";CHECK(list(&s,&c,"albums:0",e,256)==VT_NETWORK_AUTH_FAILED);status=401;CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==VT_NETWORK_AUTH_FAILED);status=200;
 fixture="{broken";CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))<0);fixture="{\"subsonic-response\":{\"status\":\"ok\"}}";fail_random=1;CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))<0);fail_random=0;
 const char *bad[]={"https://user:password@host","file:///etc/passwd","https://host?token=x","https://host#frag","https://host/path\r\nx:x","https://host\\evil","https://"};for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++){snprintf(s.host,sizeof(s.host),"%s",bad[i]);CHECK(vt_personal_base_url(&s,out,sizeof(out))<0);}
 s.protocol=VT_NETWORK_IMMICH;snprintf(s.host,sizeof(s.host),"http://[::1]:2283/photos/api/");s.username[0]=0;snprintf(c.password,sizeof(c.password),"test-api-key");CHECK(vt_personal_base_url(&s,out,sizeof(out))==0&&!strcmp(out,"http://[::1]:2283/photos/api"));expect_endpoint="/photos/api/users/me";fixture="{\"id\":\"user-1\"}";CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==0);CHECK(!strcmp(last_header,"x-api-key: test-api-key"));CHECK(!strstr(last_url,c.password));
 CHECK(list(&s,&c,"",e,256)==3&&!strcmp(e[0].path,"photos:1"));expect_endpoint="/search/metadata";
 fixture="{\"assets\":{\"items\":[{\"id\":\"photo-1\",\"type\":\"IMAGE\",\"originalFileName\":\"IMG.HEIC\"},{\"id\":\"video-1\",\"type\":\"VIDEO\"}],\"nextPage\":\"2\"}}";
 CHECK(list(&s,&c,"photos:1",e,256)==2&&e[0].is_image&&!strcmp(e[0].name,"IMG.HEIC")&&e[1].is_directory==2&&!strcmp(e[1].path,"photos:2"));json_t *body=json_loads(last_body,0,NULL);CHECK(json_integer_value(json_object_get(body,"page"))==1&&!strcmp(json_string_value(json_object_get(body,"type")),"IMAGE"));json_decref(body);
 fixture="{\"assets\":{\"items\":[],\"nextPage\":null,\"nextCursor\":\"opaque+token/=\"}}";CHECK(list(&s,&c,"favorites:1",e,256)==1&&!strcmp(e[0].path,"favorites:2@opaque+token/="));CHECK(strstr(last_body,"\"isFavorite\":true"));
 fixture="{\"assets\":{\"items\":[],\"nextPage\":null,\"nextCursor\":null}}";CHECK(list(&s,&c,"favorites:2@opaque+token/=",e,256)==0);CHECK(strstr(last_body,"\"cursor\":\"opaque+token/=\""));
 expect_endpoint="/albums";fixture="[{\"id\":\"album-1\",\"albumName\":\"Holidays\"}]";CHECK(list(&s,&c,"collections:0",e,256)==1&&!strcmp(e[0].name,"Holidays")&&!strcmp(e[0].path,"collection:album-1"));
 expect_endpoint="/albums/album-1";fixture="{\"assets\":[{\"id\":\"photo-2\",\"type\":\"IMAGE\",\"originalFileName\":\"Other.jpg\"}]}";CHECK(list(&s,&c,"collection:album-1",e,256)==1&&e[0].is_image);
 expect_endpoint="/assets/photo-1/thumbnail?size=preview";fixture="\xff\xd8\xff";unsigned char *data=NULL;size_t size=0;CHECK(vt_personal_preview(&s,&c,"photo:photo-1",&data,&size,NULL)==0&&size==3);free(data);volatile int cancel=1;CHECK(vt_personal_preview(&s,&c,"photo:photo-1",&data,&size,&cancel)<0&&data==NULL&&size==0);
 fixture="<html>login</html>";CHECK(vt_personal_preview(&s,&c,"photo:photo-1",&data,&size,NULL)<0);expect_endpoint=NULL;status=403;CHECK(vt_personal_preview(&s,&c,"photo:photo-1",&data,&size,NULL)==VT_NETWORK_AUTH_FAILED);status=302;CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))<0);status=200;transport_error=VITA_HTTPS_ERROR_UNTRUSTED_CERTIFICATE;CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==VT_NETWORK_TLS_TRUST_REQUIRED);transport_error=VITA_HTTPS_ERROR_PIN_MISMATCH;CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==VT_NETWORK_TLS_PIN_MISMATCH);transport_error=0;
 before=requests;snprintf(c.password,sizeof(c.password),"key\r\nx:bad");CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==VT_NETWORK_AUTH_FAILED&&requests==before);CHECK(list(&s,&c,"collection:foo/../bar",e,256)<0);CHECK(list(&s,&c,"photos:0",e,256)<0);CHECK(list(&s,&c,"photos:99999999999999999",e,256)<0);
 /* Large playlists are sliced without losing their tail; small caller
    buffers produce matching server pagination rather than skipped records. */
 s.protocol=VT_NETWORK_NAVIDROME;snprintf(s.host,sizeof(s.host),"https://music.example");snprintf(s.username,sizeof(s.username),"user &/è");snprintf(c.password,sizeof(c.password),"p&sswörd");expect_endpoint="getPlaylist";
 json_t *songs=json_array();for(int i=0;i<101;i++){char id[32];snprintf(id,sizeof(id),"song-%d",i);json_array_append_new(songs,json_pack("{s:s,s:s,s:s,s:s,s:s,s:i}","id",id,"title",id,"artist","Singer","album","Album","suffix","flac","bitRate",800));}
 json_t *root=json_pack("{s:{s:s,s:{s:o}}}","subsonic-response","status","ok","playlist","entry",songs);char *large=json_dumps(root,JSON_COMPACT);fixture=large;
 CHECK(list(&s,&c,"playlist:all",e,256)==101&&e[100].is_directory==2&&!strcmp(e[100].path,"playlist:all@100"));CHECK(!strcmp(e[0].artist,"Singer")&&!strcmp(e[0].album,"Album")&&!strcmp(e[0].audio_format,"flac")&&e[0].audio_bitrate_kbps==800);
 CHECK(list(&s,&c,"playlist:all@100",e,256)==1&&!strcmp(e[0].path,"song:song-100"));free(large);json_decref(root);
 expect_endpoint="getAlbumList2";fixture="{\"subsonic-response\":{\"status\":\"ok\",\"albumList2\":{\"album\":[{\"id\":\"one\",\"name\":\"One\"},{\"id\":\"two\",\"name\":\"Two\"}]}}}";
 CHECK(list(&s,&c,"albums:0",e,4)==3&&!strcmp(e[2].path,"albums:2")&&e[2].is_directory==2);CHECK(strstr(last_url,"size=2&offset=0"));CHECK(list(&s,&c,"albums:2",e,4)==3&&strstr(last_url,"offset=2"));
 fixture="{\"subsonic-response\":{\"status\":\"ok\",\"status\":\"failed\"}}";expect_endpoint=NULL;CHECK(vt_personal_authenticate(&s,&c,detail,sizeof(detail))<0);
 printf("Personal media API: %d checks, %d authenticated requests passed\n",checks,requests);return 0;}
