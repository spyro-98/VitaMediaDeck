/* Actual visible IPTV scheduler: cached paths publish while network is busy. */
#define main original_ui_main
#include "ui_test.c"
#undef main
int main(void){
 char current[192]="old-cover.png";host_pending_art="new-pending-cover.png";
 ui_artwork_path_update(current,sizeof(current),host_pending_art);CHECK(!strcmp(current,"old-cover.png"));
 host_pending_ready=1;ui_artwork_path_update(current,sizeof(current),host_pending_art);CHECK(!strcmp(current,host_pending_art));
 host_pending_art=NULL;
 host_cached_art=1;catalog.count=result_count=8;catalog_top=0;catalog_visible=8;
 for(unsigned i=0;i<8;i++){results[i]=i;snprintf(catalog.items[i].url,sizeof(catalog.items[i].url),"https://example.org/channel/%u",i);snprintf(catalog.items[i].logo,sizeof(catalog.items[i].logo),"https://example.org/logo/%u.png",i);if(i%2)strcpy(catalog.items[i].artwork_path,"-");}
 artwork_thread=42;artwork_done=0;iptv_cover_tick();CHECK(artwork_thread==42&&host_art_peeks==8);
 for(unsigned i=0;i<8;i++)CHECK(!strcmp(catalog.items[i].artwork_path,"ux0:data/VitaMediaDeck/artwork/cached.png"));
 artwork_thread=-1;iptv_cover_tick();CHECK(artwork_thread==-1); /* Fresh local hits do not start I/O. */
 printf("IPTV cached artwork: %u checks; all visible cached logos restored before a busy network worker, failed placeholders recovered, no request for fresh entries.\n",checks);
}
