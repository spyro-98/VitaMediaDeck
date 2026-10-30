#define main original_ui_main
#include "ui_test.c"
#undef main
static unsigned zaps;
static int zap_player(const VtHwPlayerScreenSource *source){
 CHECK(created==destroyed+1);CHECK(source->channel_navigation==3);CHECK(zaps<100);
 const char *expected=zaps%2?"Channel A":"Channel B";CHECK(!strcmp(source->title,expected));
 return ++zaps<80?VT_HW_PLAYER_ACTION_NEXT_CHANNEL:0;
}
int main(void){
 catalog.count=result_count=3;catalog_focus=0;catalog_top=0;
 catalog.items[0]=(VtIptvChannel){.url="https://fixture.test/b.m3u8",.title="Channel B",.mode=VT_IPTV_HLS,.name_source=VT_IPTV_NAME_CUSTOM};
 catalog.items[1]=(VtIptvChannel){.url="https://fixture.test/nested.m3u",.title="Nested list",.mode=VT_IPTV_LIST};
 catalog.items[2]=(VtIptvChannel){.url="https://fixture.test/a.m3u8",.title="Channel A",.mode=VT_IPTV_HLS,.name_source=VT_IPTV_NAME_CUSTOM};
 results[0]=0;results[1]=1;results[2]=2;strcpy(link,catalog.items[0].url);mode=VT_IPTV_HLS;
 CHECK(adjacent_channel(link,1)==2&&adjacent_channel(link,-1)==2);
 CHECK(adjacent_channel("https://unrelated.test/a.m3u8",1)==-1);
 next_event=next_result=0;live_player_observer=zap_player;const char *message="";
 CHECK(play_link(&message)==UI_SECTION_NONE);CHECK(zaps==80&&created==destroyed&&created==80);
 CHECK(catalog_focus==2&&!strcmp(link,catalog.items[2].url));
 printf("Zapping: %u checks; 80 iterative handoffs, one live source, joined cleanup before each reopen, filtered ordering and nested-list exclusion passed.\n",checks);
}
