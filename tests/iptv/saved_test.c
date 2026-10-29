/* Real JSON persistence, isolated from the repository and mounted console. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "network/iptv_catalog.h"
static int fail_commit;
static int commit_rename(const char *from,const char *to) {
    if(fail_commit && strstr(from,".tmp")){errno=EIO;return -1;}
    return rename(from,to);
}
#define rename commit_rename
#include "../../src/network/iptv_saved.c"
#undef rename
static unsigned checks;
#define CHECK(x) do {checks++;assert(x);} while(0)
static VtIptvCatalog catalog;
static void search_tests(void) {
    VtIptvChannel item={.title="RaiNews 24",.group="Notizie;Informazione",.channel_id="RaiNews24.it@HD",
        .url="https://example.org/italia.m3u?secret-token"};
    CHECK(vt_iptv_channel_matches(&item,"rai news"));
    CHECK(vt_iptv_channel_matches(&item,"RAI-NEWS24"));
    CHECK(vt_iptv_channel_matches(&item,"Informazione"));
    CHECK(vt_iptv_channel_matches(&item,"rai news24 it"));
    CHECK(vt_iptv_channel_matches(&item,"italia"));
    CHECK(!vt_iptv_channel_matches(&item,"secret-token"));
    CHECK(!vt_iptv_channel_matches(&item,"Pluto"));
    CHECK(vt_iptv_channel_matches(&item,"   "));
    strcpy(item.title,"Città TV Élite");CHECK(vt_iptv_channel_matches(&item,"CITTA tv elite"));
    strcpy(item.title,"日本語");CHECK(vt_iptv_channel_matches(&item,"日本語"));
    const char *m3u="#EXTM3U\n#EXTINF:-1 tvg-id=\"RaiNews24.it\" group-title=\"News;General\",RaiNews24\nhttps://example.org/news.m3u8\n";
    CHECK(!vt_iptv_catalog_parse(m3u,strlen(m3u),"https://example.org/list.m3u",&catalog));
    CHECK(!strcmp(catalog.items[0].group,"News;General") && !strcmp(catalog.items[0].channel_id,"RaiNews24.it"));
}
static void parse_tests(void) {
    /* Real channel lists can exceed the playback-manifest cap. Comments keep
     * this fixture below the entry cap while exercising the complete body. */
    size_t large_size=VT_IPTV_CATALOG_LIMIT;char *large=malloc(VT_IPTV_CATALOG_LIMIT+1);CHECK(large);
    const char *entry="#EXTM3U\n#EXTINF:-1,Channel\nhttps://example.org/live.m3u8\n";
    size_t at=strlen(entry);memcpy(large,entry,at);
    while(at+1024<large_size){large[at++]='#';memset(large+at,'a',1022);at+=1022;large[at++]='\n';}
    CHECK(at>VT_IPTV_PLAYLIST_LIMIT);
    CHECK(!vt_iptv_catalog_parse(large,at,"https://example.org/list.m3u",&catalog));
    CHECK(catalog.count==1&&!catalog.truncated);
    memset(large+at,' ',large_size-at);
    CHECK(!vt_iptv_catalog_parse(large,large_size,"https://example.org/list.m3u",&catalog));
    CHECK(vt_iptv_catalog_parse(large,VT_IPTV_CATALOG_LIMIT+1,"https://example.org/list.m3u",&catalog)==VT_IPTV_CATALOG_BODY_LIMIT);
    char overlong[4200];memset(overlong,'x',sizeof(overlong));memcpy(overlong,"#EXTM3U\n",8);
    CHECK(vt_iptv_catalog_parse(overlong,sizeof(overlong),"https://example.org/list.m3u",&catalog)==VT_IPTV_CATALOG_LINE_LIMIT);
    large[20]=0;CHECK(vt_iptv_catalog_parse(large,at,"https://example.org/list.m3u",&catalog)==VT_IPTV_INVALID);free(large);

    const char *m3u="\xef\xbb\xbf \t#EXTM3U x-tvg-url=\"https://example.org/epg.xml\" \r\n"
        "\n #EXTINF:-1 tvg-logo=\"logo.png\",Channel, with comma\r\n"
        "#EXTVLCOPT:http-user-agent=HbbTV/1.6.1\r\n  http://example.org/live.m3u8 \t\r\n"
        "#EXTINF:-1,Relative channel\nrelative.m3u8\n"
        "#EXTINF:3600,Movie\nmovie.mp4?token=private\n";
    CHECK(!vt_iptv_catalog_parse(m3u,strlen(m3u),"https://example.org/list.m3u",&catalog));
    CHECK(catalog.count==3 && !catalog.truncated);
    CHECK(!strcmp(catalog.items[0].guide_url,"https://example.org/epg.xml")&&!strcmp(catalog.items[2].guide_url,"https://example.org/epg.xml"));
    CHECK(!strcmp(catalog.items[0].url,"http://example.org/live.m3u8"));
    CHECK(!strcmp(catalog.items[0].logo,"https://example.org/logo.png"));
    CHECK(!strcmp(catalog.items[0].title,"Channel, with comma"));
    CHECK(!strcmp(catalog.items[0].user_agent,"HbbTV/1.6.1"));
    CHECK(!strcmp(catalog.items[1].url,"https://example.org/relative.m3u8"));
    CHECK(!catalog.items[1].user_agent[0] && catalog.items[2].mode==VT_IPTV_FILE);
    char checked[VT_IPTV_URL_MAX];
    CHECK(vt_iptv_resolve_url("https://example.org/list.m3u8","http://example.org/segment.ts",checked,sizeof(checked))==VT_IPTV_INVALID);
    const char *bad[]={"<html>Not an M3U</html>","#EXTM3UX\nhttps://example.org/live.m3u8\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,\nhttps://example.org/live.ts\n",
        "#EXTM3U\n#EXTVLCOPT:http-user-agent=Agent\rInjected\nhttps://example.org/a.m3u8\n",
        "#EXTM3U\nfile:///private/local\nhttps://user:password@example.org/live\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)
        CHECK(vt_iptv_catalog_parse(bad[i],strlen(bad[i]),"https://example.org/list.m3u",&catalog)<0);
    char many[16000]="#EXTM3U\n";size_t used=strlen(many);
    for(unsigned i=0;i<VT_IPTV_CHANNEL_MAX+1;i++)used+=snprintf(many+used,sizeof(many)-used,"https://example.org/%u.m3u8\n",i);
    CHECK(!vt_iptv_catalog_parse(many,used,"https://example.org/list.m3u",&catalog));
    CHECK(catalog.count==VT_IPTV_CHANNEL_MAX && catalog.truncated);
    char agent[VT_IPTV_USER_AGENT_MAX+1];memset(agent,'A',sizeof(agent));agent[sizeof(agent)-1]=0;
    CHECK(!vt_iptv_user_agent_valid(agent));agent[VT_IPTV_USER_AGENT_MAX-1]=0;
    CHECK(vt_iptv_user_agent_valid(agent));
    char long_agent[1024];
    snprintf(long_agent,sizeof(long_agent),"#EXTM3U\n#EXTVLCOPT:http-user-agent=%s\nhttps://example.org/live.m3u8\n",agent);
    CHECK(!vt_iptv_catalog_parse(long_agent,strlen(long_agent),"https://example.org/list.m3u",&catalog));
    CHECK(!strcmp(catalog.items[0].user_agent,agent));
}
int main(int argc,char **argv) {
    if(argc>1) {
        FILE *fixture=fopen(argv[1],"rb");CHECK(fixture);
        char *body=malloc(VT_IPTV_CATALOG_LIMIT);CHECK(body);
        size_t size=fread(body,1,VT_IPTV_CATALOG_LIMIT,fixture);CHECK(feof(fixture));fclose(fixture);
        int result=vt_iptv_catalog_parse(body,size,"https://iptv-org.github.io/iptv/languages/ita.m3u",&catalog);
        fprintf(stderr,"Reported IPTV playlist: bytes=%zu result=%d entries=%u truncated=%d\n",size,result,catalog.count,catalog.truncated);
        CHECK(!result && !catalog.truncated);free(body);
    }
    parse_tests();search_tests();
    char directory[]="/tmp/vmd-iptv-saved.XXXXXX";
    CHECK(mkdtemp(directory) && !chdir(directory));
    CHECK(!mkdir("ux0:data",0700) && !mkdir("ux0:data/VitaMediaDeck",0700));
    VtIptvChannel list={.url="https://example.org/list.m3u",.title="Saved playlist",.mode=VT_IPTV_LIST,.starred=1,.user_agent="HbbTV/1.6.1"};
    VtIptvChannel channel={.url="https://example.org/live.m3u8",.title="Channel",.mode=VT_IPTV_HLS,.starred=1,.logo="https://example.org/logo.png"};
    strcpy(channel.guide_url,"https://example.org/guide.xml.gz,https://example.org/guide2.xml");
    strcpy(channel.guide_name,"Rai Uno");channel.guide_shift=1800;
    strcpy(channel.group,"News");strcpy(channel.channel_id,"Channel.it");
    CHECK(!vt_iptv_saved_load(&catalog,0) && !catalog.count);
    memset(list.user_agent,'A',136);list.user_agent[136]=0;
    unsigned revision=vt_iptv_saved_revision();
    CHECK(!vt_iptv_saved_set(&list) && !vt_iptv_saved_set(&channel));
    CHECK(vt_iptv_saved_revision()==revision+2);
    strcpy(channel.preferred.video_key,"0123456789abcdef");strcpy(channel.preferred.audio_key,"fedcba9876543210");
    channel.preferred.width=1920;channel.preferred.height=1080;channel.preferred.bandwidth=9000000;
    CHECK(!vt_iptv_saved_set(&channel));
    strcpy(list.title,"Renamed list");CHECK(!vt_iptv_saved_set(&list));
    CHECK(!vt_iptv_saved_load(&catalog,0) && catalog.count==2);
    revision=vt_iptv_saved_revision();
    fail_commit=1;CHECK(vt_iptv_saved_remove(list.url)<0);fail_commit=0;
    CHECK(vt_iptv_saved_revision()==revision);
    CHECK(!vt_iptv_saved_load(&catalog,0) && catalog.count==2 && !strcmp(catalog.items[0].title,list.title));
    CHECK(!vt_iptv_saved_remove(list.url));CHECK(vt_iptv_saved_revision()==revision+1);
    CHECK(!vt_iptv_saved_load(&catalog,1) && catalog.count==1);
    CHECK(!strcmp(catalog.items[0].url,channel.url) && !strcmp(catalog.items[0].logo,channel.logo));
    CHECK(!strcmp(catalog.items[0].guide_url,channel.guide_url));
    CHECK(!strcmp(catalog.items[0].guide_name,channel.guide_name)&&catalog.items[0].guide_shift==channel.guide_shift);
    CHECK(!memcmp(&catalog.items[0].preferred,&channel.preferred,sizeof(channel.preferred)));
    CHECK(!strcmp(catalog.items[0].group,channel.group) && !strcmp(catalog.items[0].channel_id,channel.channel_id));
    CHECK(!vt_iptv_saved_remove(list.url)); /* Idempotent and exact URL match. */
    CHECK(vt_iptv_saved_remove("file:///private/local")<0);
    CHECK(!vt_iptv_saved_load(&catalog,0) && catalog.count==1);
    CHECK(!vt_iptv_saved_remove(channel.url));
    CHECK(!vt_iptv_saved_load(&catalog,0) && !catalog.count);
    CHECK(!vt_iptv_saved_set(&list));
    CHECK(!rename(SAVED,BACKUP)); /* Recovery after an interrupted commit. */
    CHECK(!vt_iptv_saved_load(&catalog,0) && catalog.count==1);
    CHECK(!strcmp(catalog.items[0].user_agent,list.user_agent));
    CHECK(!vt_iptv_saved_remove(list.url));
    CHECK(!vt_iptv_saved_load(&catalog,0) && !catalog.count);
    list.name_source=VT_IPTV_NAME_CUSTOM;list.single_channel=1;strcpy(list.logo,"ux0:TV/icon.png");
    CHECK(!vt_iptv_saved_set(&list)&&!vt_iptv_saved_load(&catalog,0)&&catalog.count==1);
    CHECK(catalog.items[0].name_source==VT_IPTV_NAME_CUSTOM&&catalog.items[0].single_channel&&!strcmp(catalog.items[0].logo,list.logo));
    CHECK(!vt_iptv_saved_remove(list.url));
    FILE *f=fopen(SAVED,"wb");CHECK(f);CHECK(fputs("broken",f)>=0);CHECK(!fclose(f));
    CHECK(vt_iptv_saved_set(&list)<0 && vt_iptv_saved_remove(list.url)<0);
    f=fopen(SAVED,"rb");CHECK(f);char bytes[16]={0};CHECK(fread(bytes,1,sizeof(bytes),f)==6);fclose(f);
    CHECK(!strcmp(bytes,"broken"));
    printf("IPTV import and atomic saved-entry deletion: %u checks passed (%s)\n",checks,directory);
}
