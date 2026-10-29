#define main original_ui_main
#include "ui_test.c"
#undef main
static int has_action(UiContextAction *items,int count,int action){for(int i=0;i<count;i++)if(items[i].action==action)return 1;return 0;}
int main(void){
 const char *message="";new_link();memset(&current_list,0,sizeof(current_list));memset(&stored_channel,0,sizeof(stored_channel));
 strcpy(current_list.url,"https://list.test/tv.m3u");strcpy(current_list.title,"My playlist");current_list.mode=VT_IPTV_LIST;
 catalog.count=1;catalog.items[0]=(VtIptvChannel){0};strcpy(catalog.items[0].url,"https://stream.test/channel.m3u8");strcpy(catalog.items[0].channel_id,"Rai1.it");
 strcpy(catalog.items[0].guide_name,"Rai Uno");catalog.items[0].guide_shift=1800;
 saved_view=picker_focus=0;filter_dirty=1;UiContextAction actions[16];int count=options_for_page(actions);
 CHECK(count<=16&&has_action(actions,count,24)&&!has_action(actions,count,25));
 rename_input=NULL;CHECK(!edit_guide(0,&message)&&!current_list.guide_url[0]);
 rename_input="https://epg.test/guide.xml.gz";CHECK(edit_guide(0,&message)==1);
 CHECK(!strcmp(current_list.guide_url,rename_input)&&!strcmp(stored_channel.guide_url,rename_input));
 CHECK(!stored_channel.starred&&!strcmp(catalog.items[0].guide_url,rename_input));
 count=options_for_page(actions);CHECK(count<=16&&has_action(actions,count,25));
 rename_input="https://different.test/new.xml";write_failure=1;
 CHECK(edit_guide(0,&message)<0&&!strcmp(current_list.guide_url,"https://epg.test/guide.xml.gz"));write_failure=0;
 ProgramContext context={.channel=catalog.items[0],.language="it"};snprintf(context.playlist,sizeof(context.playlist),"%s",current_list.url);VtIptvProgram p;volatile int cancel=0;
 CHECK(!load_player_program(&context,&p,&cancel,1));
 CHECK(last_guide_query.refresh==1&&last_guide_query.shift_seconds==1800&&!strcmp(last_guide_query.channel,"Rai1.it")&&!strcmp(last_guide_query.name,"Rai Uno"));
 /* Reset must reload the playlist URL, even after a channel left its URL in link. */
 input_list_fixture="#EXTM3U x-tvg-url='https://source.test/epg.xml'\n#EXTINF:-1 tvg-id='Rai1.it',Rai 1\nhttps://stream.test/channel.m3u8\n";
 strcpy(link,"https://stream.test/channel.m3u8");
 CHECK(edit_guide(1,&message)==1&&!current_list.guide_url[0]&&!stored_channel.guide_url[0]);
 CHECK(!strcmp(input_list_requested,current_list.url)&&!strcmp(catalog.items[0].guide_url,"https://source.test/epg.xml"));
 /* Recovered metadata for a library recent item is fetched only on Programme, not player startup. */
 VtIptvChannel recent={0};strcpy(recent.url,"https://stream.test/channel.m3u8");
 context=(ProgramContext){.channel=recent,.playlist="https://list.test/tv.m3u",.language="it"};
 input_list_fixture="#EXTM3U x-tvg-url='https://epg.test/public.xml'\n#EXTINF:-1 tvg-id='Rai1.it' tvg-name='Rai Uno',Rai 1\nhttps://stream.test/channel.m3u8\n";
 CHECK(!load_player_program(&context,&p,&cancel,0));
 CHECK(!strcmp(context.channel.channel_id,"Rai1.it")&&!strcmp(last_guide_query.url,"https://epg.test/public.xml"));
 printf("Programme UI flow: %u checks; R1 guide configure/cancel/failure/reset affordance, list persistence, metadata/shift/refresh routing and lazy recent-item recovery passed.\n",checks);
}
