#ifndef VMD_LOCAL_CATALOG_SUMMARY_H
#define VMD_LOCAL_CATALOG_SUMMARY_H
#include <stdio.h>
#include "ui/local_media_screen.h"
/* Kept byte-compatible with the existing VTROOT01 disk entries. */
typedef struct {
    uint32_t counts[3];
    uint64_t sizes[3];
    char preview_path[3][VT_LOCAL_MEDIA_PATH_MAX];
    uint64_t preview_size[3];
} UiLocalRootSummary;
static inline void ui_local_summary_add(UiLocalRootSummary *root,const VtLocalMediaItem *item) {
    if(!root||!item||item->source!=VT_LOCAL_MEDIA_SOURCE_FILE)return;
    int type=(int)item->type-1;if(type<0||type>=3)return;
    root->counts[type]++;root->sizes[type]+=item->size;
    if(root->preview_path[type][0])return;
    const char *preview=item->artwork_path[0]?item->artwork_path:
        item->type==VT_LOCAL_MEDIA_IMAGE||item->type==VT_LOCAL_MEDIA_VIDEO?item->path:NULL;
    if(preview){
        snprintf(root->preview_path[type],sizeof(root->preview_path[type]),"%s",preview);
        root->preview_size[type]=item->artwork_path[0]?item->artwork_size:item->size;
    }
}
#endif
