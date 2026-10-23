#include "network/dash_manifest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/parseutils.h>

static unsigned checks;
#define CHECK(x) do { checks++;assert(x); } while(0)
static const char *base="https://fixture.test/path/index.mpd";
static char xml[32768];
static int64_t utc;
static VtDashManifest *manifest;
static void build(const char *root,const char *video,const char *audio,const char *extra) {
    snprintf(xml,sizeof(xml),"<MPD xmlns='urn:mpeg:dash:schema:mpd:2011' %s><BaseURL>../media/</BaseURL>"
        "<Period id='p' start='PT0S'><AdaptationSet mimeType='video/mp4' codecs='avc1.4d401e'>"
        "<Representation id='video' bandwidth='700000' width='640' height='360'>%s</Representation></AdaptationSet>"
        "<AdaptationSet mimeType='audio/mp4' codecs='mp4a.40.2'><Representation id='audio' bandwidth='96000'>%s</Representation>"
        "</AdaptationSet></Period>%s</MPD>",root,video,audio,extra);
}
static const char *fixed="<SegmentTemplate timescale='1000' duration='2000' startNumber='7' initialization='$RepresentationID$-init.mp4' media='$RepresentationID$-$Number%05d$.m4s'/>";
static int parse(void) {return vt_dash_manifest_parse(xml,strlen(xml),base,utc,NULL,&manifest);}
int main(void) {
    CHECK(av_parse_time(&utc,"2026-09-07T12:00:00Z",0)==0);
    build("type='static' mediaPresentationDuration='PT10S'",fixed,fixed,"");CHECK(parse()==0);
    CHECK(!manifest->dynamic&&manifest->duration_us==10000000&&manifest->tracks[0].count==5);
    CHECK(!strcmp(manifest->tracks[0].initialization,"https://fixture.test/media/video-init.mp4"));
    VtDashSegment segment;
    CHECK(vt_dash_start(manifest,0,0,&segment)==0&&segment.number==7&&segment.time==0);
    CHECK(!strcmp(segment.url,"https://fixture.test/media/video-00007.m4s"));
    for(int i=0;i<5;i++){CHECK(vt_dash_segment(manifest,0,(uint64_t)i*2000,&segment)==0);CHECK(segment.start_us==i*2000000&&segment.end_us==(i+1)*2000000);}
    CHECK(vt_dash_segment(manifest,0,10000,&segment)==VT_IPTV_END);vt_dash_manifest_free(manifest);
    const char *timeline="<SegmentTemplate timescale='90000' presentationTimeOffset='900000' initialization='init-$RepresentationID$.mp4' media='$RepresentationID$-$Time$.m4s'><SegmentTimeline><S t='900000' d='180000' r='-1'/></SegmentTimeline></SegmentTemplate>";
    build("type='static' mediaPresentationDuration='PT10S'",timeline,timeline,"");CHECK(parse()==0);
    CHECK(vt_dash_start(manifest,0,4000000,&segment)==0&&segment.time==1260000&&segment.start_us==4000000);
    CHECK(!strcmp(segment.url,"https://fixture.test/media/video-1260000.m4s"));
    CHECK(manifest->tracks[0].count==5);vt_dash_manifest_free(manifest);
    const char *list="<SegmentList timescale='1' duration='2'><Initialization sourceURL='init.mp4'/><SegmentURL media='one.m4s'/><SegmentURL media='two.m4s'/></SegmentList>";
    build("mediaPresentationDuration='PT4S'",list,list,"");CHECK(parse()==0);
    CHECK(vt_dash_start(manifest,1,2000000,&segment)==0&&!strcmp(segment.url,"https://fixture.test/media/two.m4s"));vt_dash_manifest_free(manifest);
    build("type='dynamic' availabilityStartTime='2026-09-07T11:59:40Z' minimumUpdatePeriod='PT1.5S'",fixed,fixed,"");CHECK(parse()==0);
    CHECK(manifest->dynamic&&manifest->available_us==20000000&&manifest->update_ms==1500);
    CHECK(manifest->tracks[0].count==10);
    CHECK(vt_dash_segment(manifest,0,20000,&segment)==VT_IPTV_WAIT);vt_dash_manifest_free(manifest);
    const char *moving="<SegmentTemplate timescale='1000' initialization='init.mp4' media='$Time$.m4s'><SegmentTimeline><S t='12000' d='2000' r='-1'/></SegmentTimeline></SegmentTemplate>";
    build("type='dynamic' availabilityStartTime='2026-09-07T11:59:40Z'",moving,moving,"");CHECK(parse()==0);
    CHECK(manifest->tracks[0].first_us==12000000&&manifest->tracks[0].end_us==20000000);
    CHECK(vt_dash_start(manifest,0,18000000,&segment)==0&&segment.time==18000);vt_dash_manifest_free(manifest);
    const char *protected="<ContentProtection schemeIdUri='urn:mpeg:dash:mp4protection:2011' value='cenc'/><SegmentTemplate initialization='i' media='m' duration='2'/>";
    build("mediaPresentationDuration='PT4S'",protected,fixed,"");CHECK(parse()==VT_IPTV_UNSUPPORTED&&!manifest);
    build("mediaPresentationDuration='PT4S'",fixed,fixed,"<Period/>");CHECK(parse()==VT_IPTV_UNSUPPORTED);
    const char *overflow="<SegmentTemplate initialization='i' media='m' timescale='1'><SegmentTimeline><S d='2' r='9223372036854775807'/></SegmentTimeline></SegmentTemplate>";
    build("mediaPresentationDuration='PT4S'",overflow,fixed,"");CHECK(parse()!=0&&!manifest);
    const char *range="<SegmentList duration='2'><Initialization sourceURL='i' range='0-100'/><SegmentURL media='m'/></SegmentList>";
    build("mediaPresentationDuration='PT2S'",range,fixed,"");CHECK(parse()==VT_IPTV_UNSUPPORTED);
    const char *unsafe="<SegmentTemplate initialization='http://fixture.test/init.mp4' media='m' duration='2'/>";
    build("mediaPresentationDuration='PT4S'",unsafe,fixed,"");CHECK(parse()==VT_IPTV_UNSUPPORTED);
    const char *entity="<!DOCTYPE MPD [<!ENTITY secret SYSTEM 'file:///etc/passwd'>]><MPD>&secret;</MPD>";
    CHECK(vt_dash_manifest_parse(entity,strlen(entity),base,utc,NULL,&manifest)==VT_IPTV_INVALID&&!manifest);
    for(size_t n=1;n<strlen(xml);n+=17)CHECK(vt_dash_manifest_parse(xml,n,base,utc,NULL,&manifest)!=0&&!manifest);
    /* Prefer the best allowed rendition, never 1080p even below 4 Mbps. */
    snprintf(xml,sizeof(xml),"<MPD mediaPresentationDuration='PT10S'><Period>"
        "<AdaptationSet mimeType='video/mp4' codecs='avc1.4d401f'>%s"
        "<Representation id='sd' bandwidth='700000' width='640' height='360'/>"
        "<Representation id='hd' bandwidth='2000000' width='1280' height='720' frameRate='25'/>"
        "<Representation id='fhd' bandwidth='3000000' width='1920' height='1080'/>"
        "</AdaptationSet><AdaptationSet mimeType='audio/mp4' codecs='mp4a.40.2'>%s"
        "<Representation id='audio' bandwidth='96000'/></AdaptationSet></Period></MPD>",fixed,fixed);
    CHECK(parse()==0);CHECK(manifest->tracks[0].width==1280&&manifest->tracks[0].height==720);
    vt_dash_manifest_free(manifest);
    VtIptvSourceConfig cap={.width_limit=960,.height_limit=540};
    CHECK(vt_dash_manifest_parse(xml,strlen(xml),base,utc,&cap,&manifest)==0);
    CHECK(manifest->tracks[0].width==640);vt_dash_manifest_free(manifest);
    printf("DASH manifest: %u checks passed\n",checks);
    return 0;
}
