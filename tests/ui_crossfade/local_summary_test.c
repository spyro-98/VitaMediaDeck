#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/local_catalog_summary.h"
int main(void){
 UiLocalRootSummary root={0};VtLocalMediaItem item={.type=VT_LOCAL_MEDIA_VIDEO,.size=4000000000ULL};
 strcpy(item.path,"ux0:movies/first.mp4");ui_local_summary_add(&root,&item);
 assert(root.counts[0]==1&&root.sizes[0]==4000000000ULL&&!strcmp(root.preview_path[0],item.path));
 strcpy(item.path,"ux0:movies/second.mp4");ui_local_summary_add(&root,&item);
 assert(root.counts[0]==2&&root.sizes[0]==8000000000ULL&&strstr(root.preview_path[0],"first.mp4"));
 item.type=VT_LOCAL_MEDIA_AUDIO;strcpy(item.artwork_path,"ux0:music/cover.jpg");item.artwork_size=1024;ui_local_summary_add(&root,&item);
 assert(root.counts[1]==1&&root.preview_size[1]==1024&&!strcmp(root.preview_path[1],item.artwork_path));
 item.type=VT_LOCAL_MEDIA_IMAGE;item.artwork_path[0]=0;strcpy(item.path,"ux0:picture/photo.jpg");ui_local_summary_add(&root,&item);
 assert(root.counts[2]==1&&!strcmp(root.preview_path[2],item.path));
 UiLocalRootSummary before=root;item.type=0;ui_local_summary_add(&root,&item);item.type=VT_LOCAL_MEDIA_AUDIO;item.source=VT_LOCAL_MEDIA_SOURCE_NAVIDROME;ui_local_summary_add(&root,&item);
 assert(!memcmp(&before,&root,sizeof(root)));
 puts("Worker-side local summaries: typed counts, >4 GiB totals, representative art and provider exclusion passed");return 0;
}
