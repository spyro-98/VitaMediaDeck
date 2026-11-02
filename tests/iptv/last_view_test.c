/* Actual IPTV entry loop with cold UI state and retained preference storage.
 * Stop after the first frame, as a forced close must not run Back navigation. */
#define main previous_ui_main
#include "ui_test.c"
#undef main
#include <setjmp.h>
static jmp_buf closed;
static int expected_view,expected_split;
static void first_frame(unsigned tick){
 CHECK(tick==0&&saved_view&&saved_channels==(expected_view&1)&&starred_view==((expected_view>>1)&1));
 CHECK(picker_focus==(expected_split&&!(expected_view&1)));
 CHECK(result_count==((expected_split&&!(expected_view&1))?0u:1u));
 CHECK(host_last_iptv_view==expected_view&&host_iptv_view_writes==0);
 longjmp(closed,1);
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 host_saved_catalog=calloc(1,sizeof(*host_saved_catalog));CHECK(host_saved_catalog);host_saved_catalog->count=2;
 host_saved_catalog->items[0]=(VtIptvChannel){.url="https://fixture.test/list.m3u",.title="My playlist",.mode=VT_IPTV_LIST,.starred=1};
 host_saved_catalog->items[1]=(VtIptvChannel){.url="https://fixture.test/channel.m3u8",.title="My channel",.mode=VT_IPTV_HLS,.starred=1};
 for(expected_view=0;expected_view<4;expected_view++)for(expected_split=0;expected_split<2;expected_split++){
  new_link();catalog.count=0;saved_view=saved_channels=starred_view=0;search[0]=0;
  host_last_iptv_view=expected_view;host_iptv_view_writes=0;host_iptv_split=expected_split;
  frame=script_count=axis_count=0;online_case=0;tap_frame=-1;frame_observer=first_frame;
  if(!setjmp(closed))ui_iptv_screen();
  CHECK(host_last_iptv_view==expected_view);
 }
 frame_observer=NULL;iptv_cover_stop();free(host_saved_catalog);host_saved_catalog=NULL;FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("IPTV cold entry: %u checks; playlists/channels/favourites, split/unified presentation, correct focused pane, no startup rewrite and forced-close retention passed.\n",checks);return 0;
}
