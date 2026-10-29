#include "network/iptv_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static void identity(const char *body,const char *base,VtIptvChannel *v){int changed=vt_iptv_catalog_metadata(body,strlen(body),base,v);if(!changed)fprintf(stderr,"No identity from fixture: %.95s\n",body);CHECK(changed);}
int main(void){
 VtIptvChannel v={.url="https://example.test/live/playlist.m3u",.title="M3U channel list",.mode=VT_IPTV_HLS};
 const char *hls="#EXTM3U tvg-name=\"News TV\" tvg-id=\"news.it\" tvg-logo=\"../logos/news.png\"\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,A programme title\na.ts\n";
 identity(hls,v.url,&v);CHECK(!strcmp(v.title,"News TV")&&!strcmp(v.logo,"https://example.test/logos/news.png")&&!strcmp(v.channel_id,"news.it"));CHECK(v.name_source==VT_IPTV_NAME_METADATA);
 strcpy(v.title,"My custom name");v.name_source=VT_IPTV_NAME_CUSTOM;v.logo[0]=0;identity(hls,v.url,&v);CHECK(!strcmp(v.title,"My custom name")&&v.logo[0]);
 v=(VtIptvChannel){.url="ux0:TV/channel.m3u",.title="channel.m3u",.name_source=VT_IPTV_NAME_AUTO};identity(hls,v.url,&v);CHECK(!strcmp(v.title,"News TV")&&!strcmp(v.logo,"ux0:logos/news.png"));
 v=(VtIptvChannel){0};identity("#EXTM3U\n#PLAYLIST:Channel from playlist\n#EXT-X-TARGETDURATION:6\na.ts\n","https://example.test/live.m3u",&v);CHECK(!strcmp(v.title,"Channel from playlist"));
 v=(VtIptvChannel){0};identity("#EXTM3U\n#EXT-X-SESSION-DATA:DATA-ID=\"org.example.channel.name\",VALUE=\"Example TV\"\n#EXT-X-SESSION-DATA:DATA-ID=\"org.example.channel.logo\",VALUE=\"logo.png\"\n","https://example.test/live.m3u",&v);CHECK(!strcmp(v.title,"Example TV")&&!strcmp(v.logo,"https://example.test/logo.png"));
 v=(VtIptvChannel){0};CHECK(!vt_iptv_catalog_metadata("#EXTM3U\n#EXT-X-SESSION-DATA:DATA-ID=\"com.example.movie.title\",VALUE=\"A film, not a channel\"\n#EXTINF:5,Another programme\na.ts\n",strlen("#EXTM3U\n#EXT-X-SESSION-DATA:DATA-ID=\"com.example.movie.title\",VALUE=\"A film, not a channel\"\n#EXTINF:5,Another programme\na.ts\n"),"https://example.test/a",&v)&&!v.title[0]);
 v=(VtIptvChannel){0};identity("<MPD><ProgramInformation><Title>News &amp; Culture</Title></ProgramInformation></MPD>","https://example.test/a.mpd",&v);CHECK(!strcmp(v.title,"News & Culture"));
 const char *evil="<!DOCTYPE MPD [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><MPD><ProgramInformation><Title>&x;</Title></ProgramInformation></MPD>";
 v=(VtIptvChannel){0};CHECK(!vt_iptv_catalog_metadata(evil,strlen(evil),"https://example.test/a",&v)&&!v.title[0]);
 v=(VtIptvChannel){0};const char *private="#EXTM3U tvg-logo=\"ux0:data/private.png\"\n";CHECK(!vt_iptv_catalog_metadata(private,strlen(private),"https://example.test/a",&v)&&!v.logo[0]);
 const char *generic[]={"M3U channel list","M3A channel list","Lista canali M3U","Playlist · source.test","Service01","Channel · source.test"};
 for(unsigned i=0;i<sizeof(generic)/sizeof(*generic);i++){v=(VtIptvChannel){0};strcpy(v.title,generic[i]);CHECK(vt_iptv_channel_generic_name(&v));}
 VtIptvCatalog *catalog=calloc(1,sizeof(*catalog));CHECK(catalog);
 const char *m3u="#EXTM3U\n#EXTINF:-1 tvg-name=\"Radio TV\" tvg-logo=\"icon.png\",Radio TV\nhttps://example.test/a.m3u8\n";
 CHECK(!vt_iptv_catalog_parse(m3u,strlen(m3u),"ux0:TV/one.m3u",catalog));CHECK(catalog->count==1&&!strcmp(catalog->items[0].logo,"ux0:TV/icon.png")&&catalog->items[0].name_source==VT_IPTV_NAME_METADATA);
 const char *xspf="<playlist><trackList><track><location>https://example.test/a.m3u8</location><title>Example TV</title><image>logo.png</image></track></trackList></playlist>";
 CHECK(!vt_iptv_catalog_parse(xspf,strlen(xspf),"https://example.test/a.xspf",catalog));CHECK(catalog->count==1&&!strcmp(catalog->items[0].logo,"https://example.test/logo.png"));free(catalog);
 printf("Channel identity: %u checks; HLS declared name/logo/ID, session channel data, DASH title, local/remote icons, M3U/XSPF entries, custom-name preservation, generic fallback, programme-title exclusion and remote-local/entity rejection passed.\n",checks);
}
