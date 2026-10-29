#ifndef VMD_IPTV_CATALOG_FETCH_H
#define VMD_IPTV_CATALOG_FETCH_H
#include "network/iptv_catalog.h"
#include "network/iptv_source.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Shared catalogue resolver: at most three declarative HTML hops after the
 * initial fetch, cancellation between hops and explicit cycle detection.
 * Keep the saved identity intact and parse relative channels at the final URL. */
static inline int vt_iptv_catalog_fetch(VtIptvFetch fetch,void *ctx,const char *url,
 unsigned char **data,size_t *size,char *effective,size_t capacity,volatile int *cancel){
 char visited[4][VT_IPTV_URL_MAX];unsigned count=0;
 snprintf(visited[count++],sizeof(visited[0]),"%s",url);
 int result=fetch(ctx,url,VT_IPTV_CATALOG_LIMIT,data,size,effective,capacity,cancel);
 while(!result){
  if(cancel&&*cancel)return VT_IPTV_CANCELLED;
  char target[VT_IPTV_URL_MAX];
  if(vt_iptv_catalog_redirect(*data,*size,effective,target,sizeof(target)))break;
  if(count>=4||!strcmp(target,effective))return VT_IPTV_REDIRECT;
  for(unsigned i=0;i<count;i++)if(!strcmp(target,visited[i]))return VT_IPTV_REDIRECT;
  snprintf(visited[count++],sizeof(visited[0]),"%s",target);
  free(*data);*data=NULL;*size=0;
  result=fetch(ctx,target,VT_IPTV_CATALOG_LIMIT,data,size,effective,capacity,cancel);
 }
 return result==VT_IPTV_LIMIT?VT_IPTV_CATALOG_BODY_LIMIT:result;
}
#endif
