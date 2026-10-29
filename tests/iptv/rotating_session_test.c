#include "network/hls_source.h"
#include "network/iptv_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static char *read_text(const char *path,size_t *size){FILE *f=fopen(path,"rb");CHECK(f&&!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);char *p=malloc((size_t)n+1);CHECK(p&&fread(p,1,n,f)==(size_t)n);fclose(f);p[n]=0;*size=(size_t)n;return p;}
int main(int argc,char **argv){
 CHECK(argc==5);size_t a,b,n;char *old=read_text(argv[1],&a),*fresh=read_text(argv[2],&b),*reported=read_text(argv[3],&n);
 VtIptvOptions *o=calloc(1,sizeof(*o));CHECK(o);VtIptvSourceConfig cfg={.inspect=o};unsigned char *out=NULL;size_t length;
 const char *base="https://fixture.test/root/playlist.m3u";
 CHECK(!vt_hls_prepare_manifest(old,a,base,&cfg,&out,&length));free(out);CHECK(o->video_count==5);
 for(unsigned i=0;i<o->video_count;i++){
  cfg.inspect=NULL;snprintf(cfg.selection.video,sizeof(cfg.selection.video),"%s",o->video[i].id);cfg.selection.video_manual=1;
  VtIptvSelection active={0};cfg.active_selection=&active;
  CHECK(!vt_hls_prepare_manifest(fresh,b,base,&cfg,&out,&length));
  CHECK(active.video[0]&&strcmp(active.video,cfg.selection.video));free(out);
 }
 const char *duplicate="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000,RESOLUTION=640x360\na.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=1000,RESOLUTION=640x360\nb.m3u8\n";
 strcpy(cfg.selection.video,"640x360:1000::|https://old.test/session/c.m3u8");
 CHECK(vt_hls_prepare_manifest(duplicate,strlen(duplicate),base,&cfg,&out,&length)==VT_IPTV_UNSUPPORTED);
 cfg=(VtIptvSourceConfig){.inspect=o};CHECK(!vt_hls_prepare_manifest(reported,n,base,&cfg,&out,&length));
 CHECK(o->video_count==1&&o->video[0].height==1080&&o->video[0].supported&&strstr((char*)out,"chunklist.m3u8"));free(out);
 CHECK(vt_iptv_catalog_content_kind(reported,n)==VT_IPTV_HLS);
 size_t pluto_n;char *pluto=read_text(argv[4],&pluto_n);cfg=(VtIptvSourceConfig){.inspect=o};
 CHECK(!vt_hls_prepare_manifest(pluto,pluto_n,"https://fixture.test/playlist.m3u8",&cfg,&out,&length));
 CHECK(o->video_count==5&&length>2048);free(out);free(pluto);
 free(o);free(old);free(fresh);free(reported);
 printf("Actual manifest regressions: %u checks; all 5 Rakuten qualities survive refreshed session paths, ambiguous metadata rejected, reported .m3u is one supported 1080p HLS channel; real Pluto signed resource paths over 2 KiB accepted.\n",checks);
}
