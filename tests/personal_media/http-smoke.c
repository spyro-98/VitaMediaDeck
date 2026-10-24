#include "network/personal_media.h"
#include "network/network_internal.h"
#include <vita_https.h>
#include <openssl/md5.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
int vita_https_net_init(void){return 0;}void vita_https_net_term(void){}int vita_https_net_is_connected(void){return 1;}int vita_https_net_wifi_signal_percent(int*p){*p=100;return 0;}
int mbedtls_md5(const unsigned char *p,size_t n,unsigned char d[16]){MD5(p,n,d);return 0;}
int sceKernelGetRandomNumber(void*p,size_t n){static int next;memset(p,++next,n);return 0;}
int main(int argc,char **argv){assert(argc==2);assert(vita_https_init()==0);VtNetworkSource s={.protocol=VT_NETWORK_IMMICH};VtNetworkCredential c={0};snprintf(s.host,sizeof(s.host),"%s",argv[1]);snprintf(c.password,sizeof(c.password),"mock-key");char detail[192];assert(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==0);VtNetworkEntry entries[256];int n=vt_personal_list(&s,&c,"photos:1",entries,256,detail,sizeof(detail));assert(n==1&&entries[0].is_image);unsigned char *data=NULL;size_t size=0;assert(vt_personal_preview(&s,&c,entries[0].path,&data,&size,NULL)==0&&size==3);free(data);
 strcat(s.host,"/redirect");assert(vt_personal_authenticate(&s,&c,detail,sizeof(detail))<0);
 snprintf(s.host,sizeof(s.host),"%s/oversize",argv[1]);assert(vt_personal_authenticate(&s,&c,detail,sizeof(detail))<0);
 snprintf(s.host,sizeof(s.host),"%s",argv[1]);s.protocol=VT_NETWORK_NAVIDROME;snprintf(s.username,sizeof(s.username),"user");snprintf(c.password,sizeof(c.password),"password");assert(vt_personal_authenticate(&s,&c,detail,sizeof(detail))==0);n=vt_personal_list(&s,&c,"albums:0",entries,256,detail,sizeof(detail));assert(n==1&&entries[0].is_directory);
 snprintf(s.host, sizeof(s.host), "%s", argv[1]);
 s.protocol = VT_NETWORK_JELLYFIN;
 snprintf(c.access_token, sizeof(c.access_token), "mock-session");
 snprintf(c.user_id, sizeof(c.user_id), "user1");
 n = vt_jellyfin_list_cancel(&s, &c, "search:Night & è", entries, 256, detail, sizeof(detail),
                             NULL);
 assert(n == 2 && entries[0].is_video && !strcmp(entries[0].path, "video-1") &&
        entries[1].is_directory);
 volatile int cancelled = 1;
 assert(vt_jellyfin_list_cancel(&s, &c, "search:cancelled", entries, 256, detail, sizeof(detail),
                                &cancelled) < 0);
 s.protocol = VT_NETWORK_IMMICH;
 snprintf(c.password, sizeof(c.password), "mock-key");
 assert(vt_personal_list_cancel(&s, &c, "search:Night", entries, 256, detail, sizeof(detail),
                                NULL) == 1);
 assert(vt_personal_list_cancel(&s, &c, "search:cancelled", entries, 256, detail, sizeof(detail),
                                &cancelled) < 0);
 vita_https_shutdown();puts("Real HTTP transport: auth, POST fetch, preview, redirect rejection and response limit passed");return 0;}
