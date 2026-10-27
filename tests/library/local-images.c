/* Actual category entry functions; substitute only paged index/root records. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui/home_services.h"
#include "i18n/i18n.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"Local images entry contract failed: %s at line %d\n",#x,__LINE__); return 1; } } while (0)
typedef struct { VtLocalMediaItem item; } LocalMediaRoot;
static VtLocalMediaItem g_items[64];
static LocalMediaRoot roots[16];
static int g_root_view_count,index_count;
static int filtered_count(int filter) { (void)filter; return index_count; }
static int filtered_index(int filter,int position) { (void)filter; return position>=0&&position<index_count?position:-1; }
static LocalMediaRoot *screen_root(int position) { return position>=0&&position<g_root_view_count?&roots[position]:NULL; }
const char *vt_i18n_str(VtStringId id) { (void)id; return "Local images"; }
#include "local-images.inc"
int main(void) {
 for (int flattened=0;flattened<2;flattened++)
 for (int records=0;records<=50;records++) {
  index_count=records;g_root_view_count=records>16?16:records;
  for(int i=0;i<records;i++){memset(&g_items[i],0,sizeof(g_items[i]));g_items[i].type=VT_LOCAL_MEDIA_IMAGE;snprintf(g_items[i].path,sizeof(g_items[i].path),"ux0:picture/image-%d.jpg",i);}
  for(int i=0;i<g_root_view_count;i++){memset(&roots[i],0,sizeof(roots[i]));roots[i].item.type=VT_LOCAL_MEDIA_IMAGE;roots[i].item.is_directory=1;snprintf(roots[i].item.path,sizeof(roots[i].item.path),"ux0:picture/folder-%d",i);}
  int locals=flattened?records:g_root_view_count;
  CHECK(screen_count(3,flattened)==locals+2);
  LocalMediaRoot *root=(LocalMediaRoot*)1;
  const VtLocalMediaItem *provider=screen_item(3,0,flattened,&root);
  CHECK(provider&&home_provider_section(provider)==UI_SECTION_IMMICH&&root==NULL);
  root=(LocalMediaRoot*)1;
  const VtLocalMediaItem *local=screen_item(3,1,flattened,&root);
  CHECK(local&&local->source==VT_LOCAL_MEDIA_SOURCE_FILE&&local->type==VT_LOCAL_MEDIA_IMAGE&&local->is_directory);
  CHECK(local->path[0]==0&&local->name[0]&&root==NULL);
  CHECK(home_provider_section(local)==UI_SECTION_NONE);
#ifdef TEST_HAS_ENTRY_ACTION
  CHECK(screen_open_action(local)==UI_LOCAL_MEDIA_ACTION_BROWSE_FOLDER);
#endif
  for(int i=0;i<locals;i++) {
   root=NULL;const VtLocalMediaItem *item=screen_item(3,i+2,flattened,&root);
   CHECK(item==(flattened?&g_items[i]:&roots[i].item));
   CHECK(root==(flattened?NULL:&roots[i]));
#ifdef TEST_HAS_ENTRY_ACTION
   CHECK(screen_open_action(item)==(flattened?UI_LOCAL_MEDIA_ACTION_PLAY:UI_LOCAL_MEDIA_ACTION_BROWSE_FOLDER));
#endif
  }
  CHECK(!screen_item(3,-1,flattened,NULL));CHECK(!screen_item(3,locals+2,flattened,NULL));
  for(int filter=0;filter<=4;filter++)if(filter!=3) {
   CHECK(screen_count(filter,flattened)==locals+home_provider_count(filter));
   for(int position=0;position<screen_count(filter,flattened);position++) {
    const VtLocalMediaItem *item=screen_item(filter,position,flattened,NULL);
    int provider_index=home_provider_position(filter,position,locals);
    int index=home_local_position(filter,position,locals);
    CHECK(item==(provider_index>=0?home_provider_item(filter,provider_index):flattened?&g_items[index]:&roots[index].item));
   }
  }
 }
 printf("Local Images production entries: %u checks; empty/stale/populated index, folders/flat, preserved Immich, image paths and unrelated categories passed.\n",checks);return 0;
}
