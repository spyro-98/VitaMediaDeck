#include "network/hls_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {checks++;assert(x);} while(0)
static int prepare(const void *body,size_t size,unsigned char **out,size_t *length) {
    VtIptvSourceConfig c={.width_limit=1280,.height_limit=720,.bandwidth_limit=4000000};
    return vt_hls_prepare_manifest(body,size,"https://cdn.test/redirected/path/list.m3u8",&c,out,length);
}
int main(void) {
    const char *master="#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"English, Main\",DEFAULT=YES,URI=\"../audio.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s\",URI=\"unused.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=1920x1080\nunused.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=100000,CODECS=\"mp4a.40.2\"\nunused-audio.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS=\"avc1.42e01e, mp4a.40.2\",AUDIO=\"a\",SUBTITLES=\"s\"\nvideo.m3u8\n";
    unsigned char *out=NULL;size_t length=0;
    CHECK(!prepare(master,strlen(master),&out,&length));CHECK(length==strlen((char*)out));
    CHECK(strstr((char*)out,"https://cdn.test/redirected/audio.m3u8"));
    CHECK(strstr((char*)out,"https://cdn.test/redirected/path/video.m3u8"));
    CHECK(!strstr((char*)out,"unused"));free(out);
    const char *valid[]={
        "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\"\n#EXTINF:2,\na.ts\n",
        "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"identity\",URI=\"key.bin\"\n#EXTINF:2,\na.ts\n",
        "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\",BYTERANGE=\"100@0\"\n#EXTINF:2,\na.m4s\n",
        "\xef\xbb\xbf#EXTM3U\r\n#EXTINF:2,\r\na.ts\r\n"};
    for(unsigned i=0;i<sizeof(valid)/sizeof(*valid);i++){CHECK(!prepare(valid[i],strlen(valid[i]),&out,&length));free(out);}
    const char *invalid[]={
        "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"file:///private/key\"\n",
        "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"http://cdn.test/key\"\n",
        "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,KEYFORMAT=\"com.apple.streamingkeydelivery\",URI=\"skd://key\"\n",
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=9000000,RESOLUTION=1920x1080\na.m3u8\n",
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n",
        "#EXTM3U\n#EXTINF:2,\nhttps://user:password@cdn.test/a.ts\n"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){CHECK(prepare(invalid[i],strlen(invalid[i]),&out,&length)<0);CHECK(!out);}
    for(size_t n=0;n<strlen(master);n++){(void)prepare(master,n,&out,&length);free(out);}
    unsigned seed=17;char fuzz[512];
    for(unsigned n=0;n<2000;n++) {
        for(unsigned i=0;i<sizeof(fuzz);i++){seed=seed*1664525u+1013904223u;fuzz[i]=(char)(seed>>24);}
        if(n%2)memcpy(fuzz,"#EXTM3U\n",8);
        (void)prepare(fuzz,sizeof(fuzz),&out,&length);free(out);
    }
    printf("HLS selection/security: %u checks, truncated prefixes and 2000 fuzz inputs passed\n",checks);return 0;
}
