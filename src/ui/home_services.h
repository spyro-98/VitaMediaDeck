#ifndef VMD_HOME_SERVICES_H
#define VMD_HOME_SERVICES_H
#include <stddef.h>
#include "ui/local_media_screen.h"
#include "ui/sections_sidebar.h"
/* These records are presentation entries; the local filesystem index remains
 * byte-compatible and its positions exclude provider rows. */
static inline int home_provider_count(int filter) {
 return filter==0?2:(filter==2||filter==3)?1:0;
}
static inline const VtLocalMediaItem *home_provider_item(int filter,int position) {
 static const VtLocalMediaItem providers[2]={
  {.path="provider:navidrome",.name="Navidrome",.type=VT_LOCAL_MEDIA_AUDIO,.source=VT_LOCAL_MEDIA_SOURCE_NAVIDROME,.is_directory=1},
  {.path="provider:immich",.name="Immich",.type=VT_LOCAL_MEDIA_IMAGE,.source=VT_LOCAL_MEDIA_SOURCE_IMMICH,.is_directory=1}
 };
 if(position<0 || position>=home_provider_count(filter))return NULL;
 return &providers[filter==3?1:position];
}
/* Library appends services; category views keep their matching service first. */
static inline int home_provider_position(int filter,int position,int local_count) {
 int count=home_provider_count(filter);
 int index=filter==0?position-local_count:position;
 return index>=0&&index<count?index:-1;
}
static inline int home_local_position(int filter,int position,int local_count) {
 if(position<0||home_provider_position(filter,position,local_count)>=0)return -1;
 int index=filter==0?position:position-home_provider_count(filter);
 return index>=0&&index<local_count?index:-1;
}
static inline int home_provider_section(const VtLocalMediaItem *item) {
 if(!item)return UI_SECTION_NONE;
 return item->source==VT_LOCAL_MEDIA_SOURCE_NAVIDROME?UI_SECTION_NAVIDROME:
        item->source==VT_LOCAL_MEDIA_SOURCE_IMMICH?UI_SECTION_IMMICH:UI_SECTION_NONE;
}
#endif
