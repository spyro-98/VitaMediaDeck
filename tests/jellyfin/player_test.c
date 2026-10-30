#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ui/network_sources_screen.h"
#include "media/hw_player_screen.h"
#include "media/video_thumbnail.h"
#include "media/background_playback.h"
#include "ui/loading_screen.h"
#include "ui/sections_sidebar.h"
#include "library/catalog.h"
#include "i18n/i18n.h"
int sceKernelDelayThread(unsigned);
static unsigned checks,negotiations,directs,sessions,destroyed,encoder_stops,presents,players;
static int scenario,session_live,player_live,polls;
#define CHECK(v) do{checks++;assert(v);}while(0)
static void remote_media_id(const UiNetworkSelection *s,char id[16]){(void)s;strcpy(id,"jellyfin-test");}
static int run_remote_video_direct(const UiNetworkSelection *s,uint64_t start,int a,int b,uint64_t *p,uint64_t *d,int *oa,int *ob,int *paused){
 (void)s;(void)start;(void)a;(void)b;(void)oa;(void)ob;(void)paused;directs++;*p=13000;*d=120000;return scenario==1?-1:0;
}
#include "../../src/media/jellyfin_player.inc"
const char *vt_i18n_str(VtStringId id){(void)id;return "Loading";}
int ui_player_loading_run(const UiPlayerLoadingInfo *info,UiLoadingTaskFn fn,void *ctx,volatile int *cancel,const volatile long *a,const volatile long *b){(void)info;(void)cancel;(void)a;(void)b;if(scenario==7&&fn==jellyfin_stop_task)return UI_LOADING_TASK_FAILED;return fn(ctx);}
void ui_player_loading_present(const UiPlayerLoadingInfo *i,const volatile long *a,const volatile long *b){(void)i;(void)a;(void)b;presents++;}
int sceKernelDelayThread(unsigned u){(void)u;return 0;}
void vt_video_thumbnail_prepare_playback(void){}
void vt_background_playback_stop(void){}
void vt_hls_source_configure(VtIptvSourceConfig *c){CHECK(c->fetch==vt_jellyfin_hls_fetch);}
void vt_library_remote(const VtNetworkSource *s,const VtNetworkEntry *e,const char *path,VtLibraryItem *out){(void)s;(void)path;CHECK(!strcmp(e->path,"item-1"));memset(out,0,sizeof(*out));}
void vt_library_bind(const VtLibraryItem *v){(void)v;}
int vt_jellyfin_playback_prepare(const VtNetworkSource *s,const VtNetworkCredential *c,const char *path,uint64_t p,int force,VtJellyfinPlayback *out,volatile int *cancel){
 (void)s;(void)c;CHECK(!strcmp(path,"item-1")&&!session_live&&!player_live);negotiations++;
 if(scenario==5){*cancel=1;return -1;}
 memset(out,0,sizeof(*out));out->duration_ms=120000;out->position_ms=p;out->transcoding=scenario!=0&&(scenario!=1||force);
 snprintf(out->session_id,sizeof(out->session_id),"session-%u",negotiations);strcpy(out->media_source_id,"source-1");strcpy(out->url,"https://fixture.test/master.m3u8");
 if(scenario==2&&negotiations==2)CHECK(force&&p==90000);
 return 0;
}
VtIptvSession *vt_iptv_session_create(const char *url,const VtIptvSourceConfig *c){CHECK(!session_live&&!player_live&&url&&c->startup_segments==2);if(scenario==4)return NULL;session_live=1;sessions++;polls=0;return (void*)1;}
void vt_iptv_session_factory(VtIptvSession *s,VtDecoderStreamFactory *out){CHECK(s==(void*)1&&session_live);memset(out,0,sizeof(*out));}
int vt_hw_player_screen_run_server(const VtHwPlayerScreenSource *s,VtIptvSession *session,uint64_t *p,uint64_t *d,int *paused){
 CHECK(session==(void*)1&&session_live&&!player_live&&!s->allow_minimize&&s->requested_seek_ms);player_live=1;players++;
 CHECK(s->timeline_offset_ms==(scenario==2&&players==2?90000u:scenario==1?13000u:12345u));
 *p=s->timeline_offset_ms+1000;*d=120000;*paused=0;player_live=0;
 if(scenario==2&&players==1){*s->requested_seek_ms=90000;return VT_HW_PLAYER_ACTION_SERVER_SEEK;}
 return scenario==3?VT_HW_PLAYER_ACTION_SECTION_BASE+UI_SECTION_SETTINGS:scenario==6?-1:0;
}
void vt_iptv_session_cancel(VtIptvSession *s){CHECK(s==(void*)1&&session_live&&!player_live);}
int vt_iptv_session_stopped(VtIptvSession *s){CHECK(s==(void*)1&&session_live);return ++polls>1;}
void vt_iptv_session_destroy(VtIptvSession *s){CHECK(s==(void*)1&&session_live&&!player_live&&polls>1);session_live=0;destroyed++;}
int vt_jellyfin_playback_stop(const VtNetworkSource *s,const VtNetworkCredential *c,VtJellyfinPlayback *p,uint64_t position){(void)s;(void)c;(void)position;CHECK(!session_live&&!player_live&&p->session_id[0]);encoder_stops++;p->session_id[0]=0;return 0;}
int vt_jellyfin_hls_fetch(void *a,const char *b,size_t c,unsigned char **d,size_t *e,char *f,size_t g,volatile int *h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;return -1;}
int vt_jellyfin_hls_fetch_range(void *a,const char *b,uint64_t c,size_t d,unsigned char **e,size_t *f,char *g,size_t h,volatile int *i){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;return -1;}
int main(void){
 UiNetworkSelection s={.source={.protocol=VT_NETWORK_JELLYFIN},.path="item-1",.title="Movie"};
 for(scenario=0;scenario<=7;scenario++){
  negotiations=directs=sessions=destroyed=encoder_stops=presents=players=0;uint64_t p=12345,d=120000;int audio=0,sub=0,paused=0;
  int r=run_remote_video_fullscreen(&s,12345,0,0,&p,&d,&audio,&sub,&paused);
  CHECK(sessions==destroyed&&!session_live&&!player_live);
  if(scenario==0)CHECK(!r&&directs==1&&!sessions&&!encoder_stops);
  if(scenario==1)CHECK(!r&&directs==1&&negotiations==2&&sessions==1&&encoder_stops==1);
  if(scenario==2)CHECK(!r&&sessions==2&&encoder_stops==2&&p==91000&&presents==2);
  if(scenario==3)CHECK(r==VT_HW_PLAYER_ACTION_SECTION_NO_MINIMIZE_BASE+UI_SECTION_SETTINGS&&encoder_stops==1);
  if(scenario==4)CHECK(r<0&&!sessions&&encoder_stops==1);
  if(scenario==5)CHECK(!r&&!sessions&&!encoder_stops);
  if(scenario==6)CHECK(r<0&&encoder_stops==1&&destroyed==1);
  if(scenario==7)CHECK(!r&&encoder_stops==1&&destroyed==1);
 }
 printf("Jellyfin player: %u checks; direct/transcode, one-shot retry, seek restart, original timeline, section navigation, allocation failure, cancellation and decoder/producer/encoder cleanup ordering passed.\n",checks);
}
