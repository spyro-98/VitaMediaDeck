#include "network/iptv_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static void parse(const char *text,VtIptvCatalog *c){CHECK(!vt_iptv_catalog_parse(text,strlen(text),"ux0:downloads/root.m3u",c));}
int main(void){
 VtIptvCatalog *c=calloc(1,sizeof(*c));CHECK(c);char path[2048];
 CHECK(!vt_iptv_resource_resolve("ux0:downloads/top/file.m3u","../folder/list.txt",path,sizeof(path))&&!strcmp(path,"ux0:downloads/folder/list.txt"));
 CHECK(!vt_iptv_resource_resolve("ux0:downloads/top/file.m3u","/root/list.m3u",path,sizeof(path))&&!strcmp(path,"ux0:root/list.m3u"));
 CHECK(vt_iptv_resource_resolve("https://source.test/a","ux0:data/private",path,sizeof(path))<0);
 CHECK(vt_iptv_resource_resolve("ux0:a","../../ur0:tai/config",path,sizeof(path))<0);
 CHECK(vt_iptv_resource_resolve("ux0:a","ur0:tai/config",path,sizeof(path))<0);
 CHECK(!vt_iptv_resource_resolve("ux0:downloads/a","uma0:TV/one.m3u",path,sizeof(path))&&!strcmp(path,"uma0:TV/one.m3u"));
 const char *hls="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=4001642,CODECS=\"avc1.640028,mp4a.40.2\",RESOLUTION=1920x1080\nchunklist.m3u8\n";
 CHECK(vt_iptv_catalog_content_kind(hls,strlen(hls))==VT_IPTV_HLS);
 CHECK(vt_iptv_catalog_parse(hls,strlen(hls),"ux0:file.m3u",c)==VT_IPTV_UNSUPPORTED);
 const char *mixed="#EXTM3U\n#EXTINF:-1,Channel\nhttps://example.test/live.m3u8\n#EXTINF:-1,Child lists\nfolder/child.pls\nhttps://example.test/other.m3u\n";
 parse(mixed,c);CHECK(c->count==3&&c->items[0].mode==VT_IPTV_HLS&&c->items[1].mode==VT_IPTV_LIST&&c->items[2].mode==VT_IPTV_LIST);
 CHECK(!strcmp(c->items[1].url,"ux0:downloads/folder/child.pls"));
 parse("# links\nhttps://example.test/a.m3u8\nhttps://example.test/lists.txt\nsub/other.m3u\n",c);CHECK(c->count==3);
 parse("[playlist]\nTitle1=Before URL\nFile1=https://example.test/a.m3u8\nFile2=child.m3u\nTitle2=After URL\nNumberOfEntries=2\nVersion=2\n",c);
 CHECK(c->count==2&&!strcmp(c->items[0].title,"Before URL")&&!strcmp(c->items[1].title,"After URL"));
 parse("[playlist]\n title1 = Case spacing\n file1 = https://example.test/a.m3u8\n",c);CHECK(c->count==1&&!strcmp(c->items[0].title,"Case spacing"));
 parse("[InternetShortcut]\nURL=https://example.test/live.m3u\n",c);CHECK(c->count==1);
 parse("<?xml version='1.0'?><playlist xmlns='http://xspf.org/ns/0/'><trackList><track><location>https://example.test/live.m3u8?a=1&amp;b=2</location><title>Channel A</title></track><track><title>Collection</title><location>next.m3u</location></track></trackList></playlist>",c);
 CHECK(c->count==2&&!strcmp(c->items[0].title,"Channel A")&&strstr(c->items[0].url,"a=1&b=2"));
 parse("<ASX><ENTRY><TITLE>One</TITLE><REF href='https://example.test/a.m3u8'/><REF href='other.m3u'/></ENTRY></ASX>",c);CHECK(c->count==2);
 const char *bad="<!DOCTYPE playlist [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><playlist><trackList><track><location>&x;</location></track></trackList></playlist>";
 CHECK(vt_iptv_catalog_parse(bad,strlen(bad),"ux0:file",c)<0&&!c->count);
 unsigned char utf16[2048]={255,254};size_t n=2;for(const char *p=mixed;*p;p++){utf16[n++]=(unsigned char)*p;utf16[n++]=0;}
 CHECK(!vt_iptv_catalog_parse(utf16,n,"ux0:downloads/root.m3u",c)&&c->count==3);
 const unsigned char japanese[]={255,254,0xe5,0x65,0x2c,0x67,0x3d,0xd8,0x00,0xde};char *utf8=NULL;size_t size=0;
 CHECK(!vt_iptv_text_utf8(japanese,sizeof(japanese),&utf8,&size));CHECK(!strcmp(utf8,"日本😀"));free(utf8);
 CHECK(vt_iptv_catalog_content_kind("<?xml version='1.0'?><MPD/>",strlen("<?xml version='1.0'?><MPD/>"))==VT_IPTV_DASH);
 free(c);printf("Playlist content import: %u checks; single HLS vs catalogue, mixed channels/nested lists, M3U/TXT/PLS/XSPF/ASX/URL, UTF-16, relative local paths, independent mounts and remote-to-local/entity/traversal rejection passed.\n",checks);
}
