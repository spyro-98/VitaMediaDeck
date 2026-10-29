#define VMD_THEME_FLOW_LIBRARY
#include "theme_flow_test.c"
int main(void){
 host_cached_art=1;g_query.count=6;g_top=g_selected=0;g_context_generation=4;
 const int kinds[]={VT_ONLINE_RADIO,VT_ONLINE_SHOW,VT_ONLINE_FEED,VT_ONLINE_EPISODE};
 for(unsigned k=0;k<4;k++){int kind=kinds[k];
  for(int i=0;i<6;i++){g_query.items[i].kind=kind;snprintf(g_query.items[i].url,sizeof(g_query.items[i].url),"https://example.org/item/%d",i);snprintf(g_query.items[i].artwork_url,sizeof(g_query.items[i].artwork_url),"https://example.org/cover/%d.png",i);strcpy(g_query.items[i].artwork_path,"-");}
  g_cover_thread=42;g_cover_done=0;cover_tick(6);
  for(int i=0;i<6;i++)CHECK(!strcmp(g_query.items[i].artwork_path,"ux0:data/VitaMediaDeck/artwork/cached.png"));
  CHECK(g_cover_thread==42);g_cover_thread=-1;cover_tick(6);CHECK(g_cover_thread==-1);
 }
 printf("Audio cached artwork: %u checks; visible radio/show/feed/episode cached covers restored before pending network I/O, failed placeholders replaced, fresh images avoid new requests.\n",checks);
}
