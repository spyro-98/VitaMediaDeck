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
 /* Explicit refresh repairs a legacy saved/starred channel, retaining its
  * custom name, quality and membership. Publication runs on the UI thread. */
 catalog.items[0].logo[0]=0;catalog.items[0].name_source=VT_IPTV_NAME_CUSTOM;
 strcpy(catalog.items[0].title,"My channel");catalog.items[0].starred=1;catalog.items[0].preferred.height=720;
 stored_channel=catalog.items[0];artwork_channel=catalog.items[0];
 strcpy(host_guide_program.channel_logo,"https://example.org/new-logo.png");
 strcpy(host_guide_program.channel_name,"Canonical channel");strcpy(host_guide_program.channel_id,"channel.it");
 volatile int cancel=0;refresh_channel_artwork(&artwork_channel,NULL,&cancel);
 CHECK(last_guide_query.refresh==1&&!strcmp(artwork_channel.title,"My channel")&&!strcmp(artwork_channel.logo,host_guide_program.channel_logo));
 artwork_force=1;artwork_index=0;artwork_worker_generation=artwork_generation;
 strcpy(artwork_item.artwork_path,"ux0:data/VitaMediaDeck/artwork/cached.png");artwork_thread=42;artwork_done=1;artwork_cancel=0;
 iptv_cover_tick();CHECK(!strcmp(stored_channel.logo,host_guide_program.channel_logo)&&stored_channel.starred&&stored_channel.preferred.height==720);
 CHECK(!strcmp(stored_channel.title,"My channel")&&stored_channel.artwork_path[0]);
 /* Moving past an obsolete normal logo request cancels its I/O and clears
  * its retry delay, so returning does not inherit an artificial 30s wait. */
 artwork_thread=42;artwork_done=0;artwork_cancel=0;artwork_index=0;artwork_force=0;
 catalog_top=catalog_focus=4;catalog_visible=2;artwork_retry[0].after=999999;
 iptv_cover_tick();CHECK(artwork_cancel&&!artwork_retry[0].after);
 printf("IPTV cached artwork: %u checks; all visible cached logos restored before a busy network worker, failed placeholders recovered, no request for fresh entries.\n",checks);
}
