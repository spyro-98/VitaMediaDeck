#include "network/iptv_options.h"
#include "network/hls_source.h"
#include "network/dash_manifest.h"
#include "network/iptv_catalog.h"
#include "ui/iptv_player_panel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/parseutils.h>
#include <zlib.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static const char *body;static size_t body_length;static const char *base="https://cdn.test/path/index.m3u8";
int vt_iptv_http_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective,size_t cap,volatile int *cancel){
 (void)ctx;(void)url;(void)cancel;*size=body_length?body_length:strlen(body);CHECK(*size<=limit);*data=malloc(*size);CHECK(*data);memcpy(*data,body,*size);snprintf(effective,cap,"%s",base);return 0;
}
static const char *hls="#EXTM3U\n"
 "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"stereo\",NAME=\"Italiano\",LANGUAGE=\"it\",DEFAULT=YES,URI=\"it.m3u8\"\n"
 "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"stereo\",NAME=\"English\",LANGUAGE=\"en\",URI=\"en.m3u8\"\n"
 "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"other\",NAME=\"Other group\",URI=\"other.m3u8\"\n"
 "#EXT-X-STREAM-INF:BANDWIDTH=9000000,RESOLUTION=1920x1080,CODECS=\"avc1.640028,mp4a.40.2\",AUDIO=\"stereo\"\nhd.m3u8?token=private\n"
 "#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=640x360,CODECS=\"avc1.42e01e,mp4a.40.2\",AUDIO=\"stereo\"\nsd.m3u8\n"
 "#EXT-X-STREAM-INF:BANDWIDTH=12000000,RESOLUTION=1920x1080,CODECS=\"hvc1.1.6.L93\",AUDIO=\"other\"\nhevc.m3u8\n";
int main(void){
 VtIptvOptions *o=calloc(1,sizeof(*o));CHECK(o);VtIptvSourceConfig cfg={0};volatile int cancel=0;body=hls;
 CHECK(!vt_iptv_options_load(base,0,&cfg,o,&cancel));CHECK(o->discovered&&o->video_count==3&&o->audio_count==3);
 CHECK(o->video[0].supported&&!o->video[2].supported&&o->video[0].height==1080&&o->video[0].bandwidth==9000000);
 VtIptvSelection selection={0};int map[VT_IPTV_CHOICE_MAX+2];CHECK(ui_iptv_choices(o,&selection,1,map)==3);
 CHECK(ui_iptv_choice_apply(o,&selection,0,0));CHECK(ui_iptv_choice_apply(o,&selection,1,1));CHECK(!ui_iptv_choice_apply(o,&selection,0,2));CHECK(!ui_iptv_choice_apply(o,&selection,1,2));
 cfg.selection=selection;unsigned char *prepared=NULL;size_t length=0;
 CHECK(!vt_hls_prepare_manifest(body,strlen(body),base,&cfg,&prepared,&length));
 CHECK(!strstr(cfg.selection.video,"token=")&&!strstr(cfg.selection.video,"private"));
 CHECK(strstr((char*)prepared,"1920x1080")&&strstr((char*)prepared,"en.m3u8")&&!strstr((char*)prepared,"it.m3u8")&&!strstr((char*)prepared,"sd.m3u8")&&!strstr((char*)prepared,"hevc.m3u8"));free(prepared);
 char *rotated=strdup(hls);CHECK(rotated);memcpy(strstr(rotated,"private"),"changed",7);
 CHECK(!vt_hls_prepare_manifest(rotated,strlen(rotated),base,&cfg,&prepared,&length));CHECK(strstr((char*)prepared,"token=changed"));free(prepared);free(rotated);
 snprintf(cfg.selection.video,sizeof(cfg.selection.video),"https://cdn.test/path/missing.m3u8");CHECK(vt_hls_prepare_manifest(body,strlen(body),base,&cfg,&prepared,&length)<0&&!prepared);
 body="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=9000000,RESOLUTION=1920x1080,CODECS=\"avc1.640028\"\nfullhd.m3u8\n";
 cfg=(VtIptvSourceConfig){0};CHECK(!vt_hls_prepare_manifest(body,strlen(body),base,&cfg,&prepared,&length));CHECK(strstr((char*)prepared,"fullhd.m3u8"));free(prepared);
 const char *segment="<SegmentTemplate timescale='1' duration='2' initialization='$RepresentationID$-i.mp4' media='$RepresentationID$-$Number$.m4s'/>";
 char xml[8192];snprintf(xml,sizeof(xml),"<MPD mediaPresentationDuration='PT10S'><ProgramInformation><Title>Source title</Title><Source>Broadcaster</Source></ProgramInformation><Period>"
  "<AdaptationSet id='v' mimeType='video/mp4' codecs='avc1.640028'>%s<Representation id='sd' width='640' height='360' bandwidth='900000'/><Representation id='hd' width='1920' height='1080' bandwidth='9000000'/></AdaptationSet>"
  "<AdaptationSet id='it' lang='it' mimeType='audio/mp4' codecs='mp4a.40.2'><BaseURL>it/</BaseURL>%s<Representation id='aac' bandwidth='128000'/></AdaptationSet>"
  "<AdaptationSet id='en' lang='en' mimeType='audio/mp4' codecs='mp4a.40.2'><BaseURL>en/</BaseURL>%s<Representation id='aac' bandwidth='128000'/></AdaptationSet></Period></MPD>",segment,segment,segment);
 body=xml;CHECK(!vt_iptv_options_load(base,1,&cfg,o,&cancel));CHECK(o->video_count==2&&o->audio_count==2&&!strcmp(o->title,"Source title")&&!strcmp(o->provider,"Broadcaster"));
 selection=(VtIptvSelection){0};CHECK(ui_iptv_choice_apply(o,&selection,0,1));CHECK(ui_iptv_choice_apply(o,&selection,1,1));cfg.selection=selection;
 VtDashManifest *m=NULL;CHECK(!vt_dash_manifest_parse(body,strlen(body),base,0,&cfg,&m));CHECK(m->tracks[0].height==1080&&m->tracks[0].bandwidth==9000000&&!strcmp(m->tracks[1].base,"https://cdn.test/path/en/"));CHECK(!strcmp(m->tracks[1].id,"aac"));vt_dash_manifest_free(m);
 VtIptvOptions identity_options;const char *old_body=body;size_t old_length=body_length;
 body="#EXTM3U tvg-name=\"Discovery TV\" tvg-logo=\"icon.png\" tvg-id=\"discovery.it\"\n#EXT-X-STREAM-INF:BANDWIDTH=4000000,RESOLUTION=1920x1080,CODECS=\"avc1.640028,mp4a.40.2\"\nvideo.m3u8\n";body_length=0;
 CHECK(!vt_iptv_options_load(base,0,NULL,&identity_options,&cancel));CHECK(!strcmp(identity_options.channel_name,"Discovery TV")&&strstr(identity_options.channel_logo,"icon.png")&&!strcmp(identity_options.channel_id,"discovery.it"));body=old_body;body_length=old_length;
 /* Only high quality remains: automatic must attempt it too. */
 char *sd=strstr(xml,"<Representation id='sd'");char *sdend=strstr(sd,"/>")+2;memmove(sd,sdend,strlen(sdend)+1);cfg.selection=(VtIptvSelection){0};
 CHECK(!vt_dash_manifest_parse(xml,strlen(xml),base,0,&cfg,&m));CHECK(m->tracks[0].height==1080);vt_dash_manifest_free(m);
 const char *guide="<?xml version='1.0'?><!DOCTYPE tv SYSTEM 'https://invalid.test/never-fetch.dtd'><tv>"
 "<programme channel='other' start='20261028110000 +0000' stop='20261028130000 +0000'><title>Wrong channel</title></programme>"
 "<programme channel='rai1' start='20261028130000 +0100' stop='20261028140000 +0100'><title lang='en'>News</title><title lang='it'>Telegiornale</title><sub-title>Edizione del giorno</sub-title><desc lang='it'>Informazioni &amp; notizie.</desc><category>Notizie</category><episode-num system='onscreen'>Ed. 12</episode-num></programme>"
 "<programme channel='rai1' start='20261028140000 +0100' stop='20261028150000 +0100'><title>Prossimo</title></programme></tv>";
 int64_t us;CHECK(!av_parse_time(&us,"2026-10-28T12:30:00Z",0));VtIptvProgram program;
 CHECK(!vt_iptv_program_parse(guide,strlen(guide),"rai1",us/1000000,"it",&program));CHECK(program.available&&!strcmp(program.title,"Telegiornale")&&!strcmp(program.next_title,"Prossimo")&&!strcmp(program.description,"Informazioni & notizie."));CHECK(program.start==us/1000000-1800&&program.end==us/1000000+1800);
 CHECK(!vt_iptv_program_parse(guide,strlen(guide),"absent",us/1000000,"en",&program)&&!program.available);
 const char *evil="<!DOCTYPE tv [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><tv><programme>&x;</programme></tv>";
 CHECK(vt_iptv_program_parse(evil,strlen(evil),"rai1",us/1000000,"it",&program)<0&&!program.available);
 for(size_t n=1;n<strlen(guide);n++){int r=vt_iptv_program_parse(guide,n,"rai1",us/1000000,"it",&program);CHECK(r<0&&!program.available);}
 unsigned char compressed[2048];z_stream z={0};CHECK(deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY)==Z_OK);
 z.next_in=(void*)guide;z.avail_in=strlen(guide);z.next_out=compressed;z.avail_out=sizeof(compressed);
 CHECK(deflate(&z,Z_FINISH)==Z_STREAM_END);body=(void*)compressed;body_length=z.total_out;deflateEnd(&z);
 cfg=(VtIptvSourceConfig){0};CHECK(!vt_iptv_program_load(base,"rai1",&cfg,"it",&program,&cancel));
 compressed[body_length-1]^=1;CHECK(vt_iptv_program_load("https://bad-guide.test/invalid.xml.gz","rai1",&cfg,"it",&program,&cancel)<0&&!program.available);body_length=0;body=hls;
 cancel=1;CHECK(vt_iptv_options_load(base,0,&cfg,o,&cancel)==VT_IPTV_CANCELLED&&!o->discovered);cancel=0;
 VtIptvCatalog *catalog=calloc(1,sizeof(*catalog));CHECK(catalog);
 const char *list="#EXTM3U x-tvg-url=\"guide.xml\"\n#EXTINF:-1 tvg-id=\"rai1\",Rai 1\nhttps://cdn.test/live.m3u8\n";
 CHECK(!vt_iptv_catalog_parse(list,strlen(list),base,catalog));CHECK(!strcmp(catalog->items[0].guide_url,"https://cdn.test/path/guide.xml")&&!strcmp(catalog->items[0].channel_id,"rai1"));const char *http_guide="#EXTM3U url-tvg=\"http://epg.test/guide.xml.gz\"\n#EXTINF:-1 tvg-id=\"rai1\",Rai 1\nhttps://cdn.test/live.m3u8\n";
 CHECK(!vt_iptv_catalog_parse(http_guide,strlen(http_guide),base,catalog));CHECK(!strcmp(catalog->items[0].guide_url,"http://epg.test/guide.xml.gz"));free(catalog);
 free(o);printf("IPTV options: %u checks; HLS/DASH 1080p at 9Mbps, explicit video/audio selection, group compatibility, unavailable IDs, XMLTV current/next/locales/time zones and entity/truncation rejection passed.\n",checks);
}
