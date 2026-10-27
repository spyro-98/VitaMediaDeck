/* Actual main.c routing; screens/filesystem/playback are scripted boundaries. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ui/home_tabs.h"
#include "ui/library_screen.h"
#include "ui/local_files_screen.h"
#include "ui/network_sources_screen.h"
#include "ui/sections_sidebar.h"
#include "ui/music_player.h"
#include "ui/image_viewer.h"
#include "i18n/i18n.h"
static unsigned checks, cursor, length;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"Browse contract failed: %s; step %u, tab %d (line %d)\n",#c,cursor,active,__LINE__);exit(1);}}while(0)
enum {DASH,CATEGORY,FILES,VIDEO,AUDIO,IMAGE};
typedef struct {int screen,tab,action,next_tab,filter;const char *root;} Step;
static Step script[32];
static int active,offline,home_resets,online_home,g_network_ready=1;
static Step consume(int screen){CHECK(cursor<length);Step s=script[cursor++];CHECK(s.screen==screen);CHECK(s.tab==active);if(s.next_tab>=0)active=s.next_tab;return s;}
static void add(int screen,int tab,int action,int next,int filter,const char *root){script[length++]=(Step){screen,tab,action,next,filter,root};}
static void reset(int tab){cursor=length=0;active=tab;offline=0;home_resets=0;online_home=-1;}
int ui_home_tab(void){return active;}
void ui_home_tab_set(int tab){active=tab;offline=0;}
int ui_home_tab_online(void){return active==HOME_RADIO||(active==HOME_PODCAST&&!offline);}
int ui_home_tab_filter(void){const int f[]={0,1,2,0,4,3};return f[active];}
void ui_home_podcast_set_offline(int value){offline=value;}
void ui_online_audio_set_home(int value){online_home=value;}
void ui_library_reset_home(void){home_resets++;}
int ui_library_screen(VtLibraryItem *item){Step s=consume(DASH);memset(item,0,sizeof(*item));item->provider=VT_LIBRARY_LOCAL;item->kind=VT_LIBRARY_VIDEO;strcpy(item->locator,"ux0:movies/sample.mp4");return s.action;}
static void fill(VtLocalMediaItem *item,int type){memset(item,0,sizeof(*item));item->type=type;strcpy(item->path,"ux0:media/selected");}
int ui_local_media_screen(VtLocalMediaItem *item){CHECK(active!=HOME_LIBRARY);Step s=consume(CATEGORY);fill(item,s.filter);if(s.root)snprintf(item->path,sizeof(item->path),"%s",s.root);return s.action;}
int ui_local_files_screen_open(const char *root,VtLocalMediaType filter,VtLocalMediaItem *item){Step s=consume(FILES);CHECK((int)filter==s.filter);CHECK((!root&&!s.root)||(root&&s.root&&!strcmp(root,s.root)));fill(item,filter?filter:VT_LOCAL_MEDIA_VIDEO);return s.action;}
static int play_local_video(const VtLocalMediaItem *item){CHECK(item->type==VT_LOCAL_MEDIA_VIDEO);return consume(VIDEO).action;}
static int play_local_audio(VtLocalMediaItem item){CHECK(item.type==VT_LOCAL_MEDIA_AUDIO);return consume(AUDIO).action;}
int ui_image_viewer_show(const char *path,const char *title){(void)path;(void)title;return consume(IMAGE).action;}
/* Non-navigation boundaries must not be reached by these fixture scenarios. */
static void unexpected(void){CHECK(0);}
typedef struct {uint64_t size;} SceIoStat;
int sceIoGetstat(const char *p,SceIoStat *s){(void)p;(void)s;return 0;}
const char *vt_i18n_str(VtStringId id){(void)id;return "fixture";}
int vt_i18n_current_language(void){return VT_LANG_EN;}
void ui_message_show(const char *a,const char *b,int ms){(void)a;(void)b;(void)ms;unexpected();}
int vt_library_source(const VtLibraryItem *v,VtNetworkSource *s,VtNetworkCredential *c){(void)v;(void)s;(void)c;unexpected();return -1;}
int ui_network_browse_saved(VtNetworkSource *s,VtNetworkCredential *c,UiNetworkSelection *n){(void)s;(void)c;(void)n;unexpected();return -1;}
static int play_remote_selection(UiNetworkSelection s){(void)s;unexpected();return -1;}
static int play_library_remote(const VtLibraryItem *v){(void)v;unexpected();return -1;}
int vt_background_playback_library_matches(const char *id){(void)id;return 0;}
int ui_online_audio_open_library(const VtLibraryItem *v){(void)v;unexpected();return -1;}
int ui_iptv_open_library(const VtLibraryItem *v){(void)v;unexpected();return -1;}
int ui_local_media_actions(const VtLocalMediaItem *i){(void)i;unexpected();return -1;}
static int local_media_in_use(const VtLocalMediaItem *i){(void)i;unexpected();return 0;}
static int rename_local_media(const VtLocalMediaItem *i){(void)i;unexpected();return -1;}
static int move_local_media(const VtLocalMediaItem *i){(void)i;unexpected();return -1;}
static int delete_local_media(const VtLocalMediaItem *i){(void)i;unexpected();return -1;}
int ui_local_media_confirm_delete(const VtLocalMediaItem *i){(void)i;unexpected();return 0;}
void ui_local_media_cache_invalidate(void){unexpected();}
void vt_library_local(const VtLocalMediaItem *i,VtLibraryItem *o){(void)i;memset(o,0,sizeof(*o));}
void vt_library_started(const VtLibraryItem *i){(void)i;}
#include "browse-local.inc"
int main(void){
 const int tabs[]={HOME_VIDEO,HOME_AUDIO,HOME_IMAGES,HOME_PODCAST};
 for(int i=0;i<4;i++){
  int tab=tabs[i],type=tab==HOME_IMAGES?VT_LOCAL_MEDIA_IMAGE:tab==HOME_VIDEO?VT_LOCAL_MEDIA_VIDEO:VT_LOCAL_MEDIA_AUDIO;
  /* Home -> category -> folder -> back to category. Offline podcast returns online. */
  reset(HOME_LIBRARY);offline=tab==HOME_PODCAST;
  add(DASH,HOME_LIBRARY,UI_LIBRARY_TAB,tab,0,NULL);
  add(CATEGORY,tab,UI_LOCAL_MEDIA_ACTION_BROWSE_FOLDER,-1,type,NULL);
  add(FILES,tab,UI_LOCAL_MEDIA_ACTION_BACK,-1,type,"ux0:media/selected");
  add(CATEGORY,tab,UI_LOCAL_MEDIA_ACTION_BACK,-1,type,NULL);
  if(tab!=HOME_PODCAST)add(DASH,HOME_LIBRARY,UI_LIBRARY_SECTION_BASE+UI_SECTION_SETTINGS,-1,0,NULL);
  CHECK(browse_local()==(tab==HOME_PODCAST?UI_SECTION_LOCAL_MEDIA:UI_SECTION_SETTINGS));CHECK(cursor==length);
  if(tab==HOME_PODCAST){CHECK(!offline);CHECK(browse_local()==UI_SECTION_ONLINE_AUDIO);CHECK(online_home==1);}
  else CHECK(home_resets==2);
  /* Play from a category and reopen that category, preserving the selected tab. */
  reset(tab);offline=tab==HOME_PODCAST;
  add(CATEGORY,tab,UI_LOCAL_MEDIA_ACTION_PLAY,-1,type,NULL);
  add(type==VT_LOCAL_MEDIA_VIDEO?VIDEO:type==VT_LOCAL_MEDIA_AUDIO?AUDIO:IMAGE,tab,type==VT_LOCAL_MEDIA_IMAGE?0:UI_SECTION_LOCAL_MEDIA,-1,0,NULL);
  add(CATEGORY,tab,UI_LOCAL_MEDIA_ACTION_SECTION_BASE+UI_SECTION_SETTINGS,-1,type,NULL);
  CHECK(browse_local()==UI_SECTION_SETTINGS);CHECK(cursor==length);CHECK(active==tab);
  /* Explicit Files action returns to the category, rather than losing its tabs. */
  reset(tab);offline=tab==HOME_PODCAST;
  add(CATEGORY,tab,UI_LOCAL_MEDIA_ACTION_BROWSE_FILES,-1,type,NULL);
  add(FILES,tab,UI_LOCAL_MEDIA_ACTION_BACK,-1,0,"");
  add(CATEGORY,tab,UI_LOCAL_MEDIA_ACTION_SECTION_BASE+UI_SECTION_SETTINGS,-1,type,NULL);
  CHECK(browse_local()==UI_SECTION_SETTINGS);CHECK(cursor==length);
  /* Selecting Home in the category returns to the dashboard on next dispatch. */
  reset(tab);offline=tab==HOME_PODCAST;
  add(CATEGORY,tab,UI_LOCAL_MEDIA_ACTION_SECTION_BASE+UI_SECTION_LOCAL_MEDIA,HOME_LIBRARY,type,NULL);
  CHECK(browse_local()==UI_SECTION_LOCAL_MEDIA);CHECK(cursor==length);
  add(DASH,HOME_LIBRARY,UI_LIBRARY_SECTION_BASE+UI_SECTION_SETTINGS,-1,0,NULL);
  CHECK(browse_local()==UI_SECTION_SETTINGS);CHECK(cursor==length);CHECK(home_resets==1);
 }
 /* Images' permanent local entry opens device roots with the image filter,
  * returns from the viewer into the same filesystem browser, then its tab. */
 reset(HOME_IMAGES);
 add(CATEGORY,HOME_IMAGES,UI_LOCAL_MEDIA_ACTION_BROWSE_FOLDER,-1,VT_LOCAL_MEDIA_IMAGE,"");
 add(FILES,HOME_IMAGES,UI_LOCAL_MEDIA_ACTION_PLAY,-1,VT_LOCAL_MEDIA_IMAGE,"");
 add(IMAGE,HOME_IMAGES,0,-1,0,NULL);
 add(FILES,HOME_IMAGES,UI_LOCAL_MEDIA_ACTION_BACK,-1,VT_LOCAL_MEDIA_IMAGE,NULL);
 add(CATEGORY,HOME_IMAGES,UI_LOCAL_MEDIA_ACTION_SECTION_BASE+UI_SECTION_SETTINGS,-1,VT_LOCAL_MEDIA_IMAGE,NULL);
 CHECK(browse_local()==UI_SECTION_SETTINGS);CHECK(cursor==length);CHECK(active==HOME_IMAGES);
 /* Direct filesystem access requested from the dashboard still returns there. */
 reset(HOME_LIBRARY);add(DASH,HOME_LIBRARY,UI_LIBRARY_LOCAL,-1,0,NULL);
 add(FILES,HOME_LIBRARY,UI_LOCAL_MEDIA_ACTION_BACK,-1,0,"");
 add(DASH,HOME_LIBRARY,UI_LIBRARY_SECTION_BASE+UI_SECTION_SETTINGS,-1,0,NULL);
 CHECK(browse_local()==UI_SECTION_SETTINGS);CHECK(cursor==length);CHECK(home_resets==2);
 /* Direct online tabs retain their existing home/provider route. */
 for(int tab=HOME_RADIO;tab<=HOME_PODCAST;tab++){
  reset(tab);CHECK(browse_local()==UI_SECTION_ONLINE_AUDIO);CHECK(online_home==1);CHECK(cursor==0);
 }
 printf("Actual main browse routing: %u assertions, category tabs/folders/playback/Back, offline/online podcast and integrated Home passed.\n",checks);
 return 0;
}
