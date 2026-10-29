#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network/iptv_catalog_fetch.h"
static unsigned checks,fetches;static int cycle,endless,cancel_after;
static volatile int cancelled;
#define CHECK(x) do{checks++;assert(x);}while(0)
static int redirect(const char *body,const char *base,char *out){return vt_iptv_catalog_redirect(body,strlen(body),base,out,VT_IPTV_URL_MAX);}
static int fake_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective,size_t cap,volatile int *cancel){
 (void)ctx;CHECK(limit==VT_IPTV_CATALOG_LIMIT&&cancel==&cancelled);fetches++;
 snprintf(effective,cap,"%s",url);char body[400];
 if(cycle)snprintf(body,sizeof(body),"<html><meta http-equiv='refresh' content='0;url=https://other.test/%s'></html>",fetches%2?"b":"a");
 else if(endless)snprintf(body,sizeof(body),"<html><meta http-equiv='refresh' content='0;url=https://other.test/%u'></html>",fetches);
 else if(fetches==1)snprintf(body,sizeof(body),"<html><meta http-equiv='refresh' content='0;url=/landing'></html>");
 else if(fetches==2)snprintf(body,sizeof(body),"<html><a href='../data/channels.m3u?key=a&amp;b=2'>Open</a></html>");
 else snprintf(body,sizeof(body),"#EXTM3U\n#EXTINF:-1,One channel\n./live.m3u8\n");
 *size=strlen(body);*data=malloc(*size);CHECK(*data);memcpy(*data,body,*size);
 if(cancel_after&&fetches==1)cancelled=1;
 return 0;
}
static unsigned char *read_file(const char *path,size_t *size){FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);unsigned char *p=malloc((size_t)n+1);CHECK(p);CHECK(fread(p,1,n,f)==(size_t)n);fclose(f);p[n]=0;*size=n;return p;}
int main(int argc,char **argv){
 char out[VT_IPTV_URL_MAX];
 CHECK(!redirect("<html><a href='/list.M3U?x=1&amp;y=2'>open</a><a href='/list.M3U?x=1&amp;y=2'>again</a></html>","https://unrelated.test/page",out));
 CHECK(!strcmp(out,"https://unrelated.test/list.M3U?x=1&y=2"));
 CHECK(redirect("<a href='/a.m3u'>a</a><a href='/b.m3u'>b</a>","https://unrelated.test/page",out)==1&&!out[0]);
 CHECK(redirect("<script>window.location='https://other.test/a.m3u';</script>","https://unrelated.test/page",out)==1);
 CHECK(redirect("<a href='http://other.test/a.m3u'>a</a>","https://unrelated.test/page",out)==1);
 CHECK(redirect("<a href='file:///a.m3u'>a</a>","https://unrelated.test/page",out)==1);
 CHECK(!redirect("<meta http-equiv='REFRESH' content=\"0; URL = '/next?x=1&amp;y=2'\"><a href='/other.m3u'>other</a>","https://unrelated.test/page",out));
 CHECK(!strcmp(out,"https://unrelated.test/next?x=1&y=2"));
 CHECK(redirect("#EXTM3U\nhttps://other.test/a.m3u8\n","https://unrelated.test/page",out)==1);
 unsigned char *data=NULL;size_t size=0;char effective[VT_IPTV_URL_MAX];
 CHECK(!vt_iptv_catalog_fetch(fake_fetch,NULL,"https://unrelated.test/start",&data,&size,effective,sizeof(effective),&cancelled));
 CHECK(fetches==3&&!strcmp(effective,"https://unrelated.test/data/channels.m3u?key=a&b=2"));
 VtIptvCatalog *catalog=calloc(1,sizeof(*catalog));CHECK(catalog);
 CHECK(!vt_iptv_catalog_parse(data,size,effective,catalog)&&catalog->count==1&&!strcmp(catalog->items[0].url,"https://unrelated.test/data/live.m3u8"));free(data);
 cycle=1;fetches=0;CHECK(vt_iptv_catalog_fetch(fake_fetch,NULL,"https://other.test/a",&data,&size,effective,sizeof(effective),&cancelled)==VT_IPTV_REDIRECT&&fetches==2);free(data);
 cycle=0;endless=1;fetches=0;CHECK(vt_iptv_catalog_fetch(fake_fetch,NULL,"https://other.test/a",&data,&size,effective,sizeof(effective),&cancelled)==VT_IPTV_REDIRECT&&fetches==4);free(data);
 endless=0;cancel_after=1;fetches=0;CHECK(vt_iptv_catalog_fetch(fake_fetch,NULL,"https://other.test/a",&data,&size,effective,sizeof(effective),&cancelled)==VT_IPTV_CANCELLED&&fetches==1);free(data);
 if(argc==5){
  size_t n=0,base_size=0,target_size=0;
  unsigned char *html=read_file(argv[1],&n),*base=read_file(argv[2],&base_size),*expected=read_file(argv[3],&target_size);
  CHECK(!vt_iptv_catalog_redirect(html,n,(char*)base,out,sizeof(out))&&!strcmp(out,(char*)expected));
  free(html);free(base);free(expected);
  data=read_file(argv[4],&size);CHECK(!vt_iptv_catalog_parse(data,size,out,catalog)&&catalog->count&&!catalog->truncated);
  printf("Reported playlist: HTML resolved to the declared M3U; %zu bytes, %u entries, no truncation. URLs omitted.\n",size,catalog->count);free(data);
 }
 free(catalog);printf("Generic catalogue redirects: %u checks; arbitrary hosts, unique/repeated/ambiguous anchors, relative paths, entities, meta refresh, no script execution, URL policy, cycles, hop limit, cancellation and final catalogue parsing passed.\n",checks);
}
