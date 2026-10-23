#include "network/iptv_source.h"
#include "../../../vita-hw-decoder/src/internal/video_timeline.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char playlist[50000];
static unsigned manifests,segments;
int vt_iptv_http_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective,size_t capacity,volatile int *cancel) {
    (void)ctx;(void)cancel;snprintf(effective,capacity,"%s",url);
    if(strstr(url,"m3u8")){manifests++;*size=strlen(playlist);*data=(unsigned char*)strdup(playlist);}
    else {segments++;*size=376;*data=calloc(1,*size);(*data)[0]=(*data)[188]=0x47;}
    assert(*data&&*size<=limit);return 0;
}
int main(void) {
    strcpy(playlist,"#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:900\n");
    for(int i=0;i<300;i++){char line[80];snprintf(line,sizeof(line),"#EXTINF:6,\nsegment-%d.ts\n",i);strcat(playlist,line);}strcat(playlist,"#EXT-X-ENDLIST\n");
    VtIptvManifest *m=malloc(sizeof(*m));assert(m);
    assert(!vt_iptv_manifest_parse_window(playlist,strlen(playlist),"https://example.org/vod.m3u8",964,m));
    assert(m->segment_count==64&&m->segments[0].sequence==964&&m->total_segments==300&&m->total_duration_ms==1800000&&m->last_sequence==1199);
    free(m);
    VtIptvSourceConfig config={.buffer_limit=1128,.segment_limit=376};
    VtIptvSource *s=vt_iptv_source_create("https://example.org/vod.m3u8",&config);assert(s);
    VtIptvCursor cursor;unsigned char bytes[376];size_t size;
    for(int i=0;i<300;i++) {
        assert(!vt_iptv_source_poll(s,(uint64_t)i*6000));
        if(!i){assert(!vt_iptv_source_begin(s,&cursor));assert(cursor.sequence==900);}
        assert(!vt_iptv_source_read(s,&cursor,bytes,sizeof(bytes),&size)&&size==376);
    }
    assert(vt_iptv_source_poll(s,1800000)==VT_IPTV_END);
    assert(vt_iptv_source_read(s,&cursor,bytes,sizeof(bytes),&size)==VT_IPTV_END);
    VtIptvSourceStatus status;vt_iptv_source_status(s,&status);
    assert(status.finite&&status.duration_ms==1800000&&manifests==1&&segments==300);
    vt_iptv_source_destroy(s);
    /* Exact mismatch from the mounted console: video 10033 ms, audio 0,
     * shared origin 10010100 us. Test the production presentation conversion. */
    assert(video_timeline_us(10033333,10010100)==23233);
    assert(video_timeline_us(3601033333LL,3601010100LL)==23233);
    assert(video_timeline_us(10033333,0)==10033333); /* Legacy files unchanged. */
    assert(video_timeline_us(-20,-30)==10);
    assert(video_timeline_us(INT64_MIN,1)==0&&video_timeline_us(INT64_MAX,-1)==0);
    puts("HLS VOD: 300 segments, 30 minutes, one manifest fetch; nonzero live PTS and legacy timeline checks passed");
}
