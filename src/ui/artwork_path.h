#ifndef VMD_UI_ARTWORK_PATH_H
#define VMD_UI_ARTWORK_PATH_H
#include "media/video_thumbnail.h"
#include <stdio.h>
#include <string.h>
/* Metadata can change the image URL as well as its bytes. Queue the new
 * texture, retaining a visible old cover until the replacement is available. */
static inline void ui_artwork_path_update(char *current,size_t capacity,const char *next){
 if(!next||!next[0]||!strcmp(current,next))return;
 if(current[0]&&current[0]!='-'&&vt_artwork_thumbnail_get(current)&&!vt_artwork_thumbnail_get(next))return;
 snprintf(current,capacity,"%s",next);
}
#endif
