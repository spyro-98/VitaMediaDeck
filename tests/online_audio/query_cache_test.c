#include "network/online_audio.h"
#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static void *concurrent(void *arg){const VtOnlineQuery*q=arg;for(int i=0;i<20;i++)assert(!vt_online_query_cache_write(q));return NULL;}
int main(void){
 mkdir("ux0:data",0700);mkdir("ux0:data/VitaMediaDeck",0700);
 VtOnlineQuery *q=calloc(1,sizeof(*q)),*out=calloc(1,sizeof(*out));assert(q&&out);
 q->kind=VT_ONLINE_EPISODE;strcpy(q->query,"https://example.org/feed?test=secret");q->count=2;q->has_more=1;
 strcpy(q->country,"IT");strcpy(q->items[0].title,"Known good");q->items[0].kind=VT_ONLINE_EPISODE;
 strcpy(q->items[0].artwork_url,"https://example.org/image.webp");strcpy(q->items[0].artwork_path,"../../unmanaged");
 strcpy(q->items[0].published_date,"2026-10-23");q->items[0].duration_ms=123456789;
 assert(!vt_online_query_cache_write(q));*out=*q;out->count=0;
 assert(!vt_online_query_cache_read(out)&&out->count==2&&out->has_more&&!strcmp(out->items[0].title,"Known good"));
 assert(!out->items[0].artwork_path[0]&&out->items[0].duration_ms==123456789);
 for(int field=0;field<6;field++) {
  *out=*q;if(field==0)out->kind++;if(field==1)out->offset++;if(field==2)out->browse++;if(field==3)out->search_field++;if(field==4)strcpy(out->country,"JP");if(field==5)strcat(out->query,"changed");
  assert(vt_online_query_cache_read(out)<0&&out->count==2);
 }
 pthread_t a,b;assert(!pthread_create(&a,NULL,concurrent,q));assert(!pthread_create(&b,NULL,concurrent,q));pthread_join(a,NULL);pthread_join(b,NULL);
 q->cancel=1;strcpy(q->items[0].title,"Cancelled");assert(vt_online_query_cache_write(q)<0);q->cancel=0;*out=*q;
 assert(!vt_online_query_cache_read(out)&&!strcmp(out->items[0].title,"Known good"));
 DIR *dir=opendir("ux0:data/VitaMediaDeck/catalogue");assert(dir);struct dirent*entry;char first[256]="";
 while((entry=readdir(dir)))if(strstr(entry->d_name,".json")){assert(strlen(entry->d_name)==69&&!strstr(entry->d_name,"secret"));snprintf(first,sizeof(first),"ux0:data/VitaMediaDeck/catalogue/%s",entry->d_name);}
 closedir(dir);assert(first[0]);FILE*f=fopen(first,"wb");assert(f);fputs("{bad",f);fclose(f);assert(vt_online_query_cache_read(out)<0&&!strcmp(out->items[0].title,"Known good"));
 for(int i=0;i<22;i++){snprintf(q->query,sizeof(q->query),"https://example.org/feed/%d",i);assert(!vt_online_query_cache_write(q));}
 dir=opendir("ux0:data/VitaMediaDeck/catalogue");assert(dir);int count=0;while((entry=readdir(dir)))count+=strstr(entry->d_name,".json")!=NULL;closedir(dir);assert(count<=16);
 free(q);free(out);puts("Persistent catalogue cache: selectors, metadata, cancellation, corruption, concurrent writes and 16-file limit passed");return 0;
}
