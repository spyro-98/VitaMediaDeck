#include <assert.h>
#include "../../../src/ui/mini_player.c"
static unsigned checks,resumes,thumbnail_resumes;
static uint64_t now=1000000;
static unsigned held;
static int thumbnails_enabled=1;
#define CHECK(x) do{checks++;assert(x);}while(0)
uint64_t sceKernelGetProcessTimeWide(void){return now;}
int sceCtrlPeekBufferPositive(int port,SceCtrlData *c,int n){(void)port;(void)n;memset(c,0,sizeof(*c));c->buttons=held;c->lx=c->ly=128;return 1;}
int vt_background_playback_snapshot(VtBackgroundPlaybackSnapshot *s){memset(s,0,sizeof(*s));s->state=VT_BACKGROUND_PLAYING;s->is_video=1;s->video_width=1280;s->video_height=720;return 1;}
void vt_background_playback_video_render_complete(void){}
int vt_background_playback_resume_fullscreen(void){resumes++;thumbnails_enabled=0;return 0;}
void vt_video_thumbnail_resume(void){thumbnail_resumes++;thumbnails_enabled=1;}
void vt_background_playback_request_stop(void){}
void vt_background_playback_seek_relative(int64_t n){(void)n;}
void vt_background_playback_toggle_pause(void){}
int ui_touch_hit_rect(int x,int y,int rx,int ry,int w,int h){return x>=rx&&x<rx+w&&y>=ry&&y<ry+h;}
int main(void) {
 g_mini_target_visible=1;g_mini_animation=1;
 unsigned pressed=0;ui_mini_player_handle_buttons(&pressed);
 held=SCE_CTRL_START;pressed=SCE_CTRL_START;
 CHECK(ui_mini_player_handle_buttons(&pressed));CHECK(!resumes);
 now+=120000;held=0;pressed=0;CHECK(ui_mini_player_handle_buttons(&pressed));
 CHECK(resumes==1&&thumbnail_resumes==1&&thumbnails_enabled);
 UiTouchEvent tap={.down_x=200,.x=200,.down_y=490,.y=490};
 CHECK(ui_mini_player_handle_touch(UI_TOUCH_EVENT_TAP,&tap));
 CHECK(resumes==2&&thumbnail_resumes==2&&thumbnails_enabled);
 printf("PASS: %u mini-player return checks\n",checks);
}
