#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "ui/audio_layout.h"
#include "ui/focus_glow.h"
#include "ui/media_grid_layout.h"
static uint64_t now=1;
static int reduced,draws,clip_top,clip_bottom,clipped,top_visible;
uint64_t sceKernelGetProcessTimeWide(void){return now;}
int vt_preferences_reduce_motion(void){return reduced;}
void vita2d_draw_rectangle(float x,float y,float w,float h,unsigned c){(void)x;(void)c;assert(w>0&&h>0);if(clipped&&h==2&&y>=clip_top&&y+h<=clip_bottom)top_visible++;draws++;}
void vita2d_set_clip_rectangle(int a,int b,int c,int d){(void)a;(void)c;clip_top=b;clip_bottom=d;}
void vita2d_enable_clipping(void){clipped=1;}
void vita2d_disable_clipping(void){clipped=0;}
int main(void){
 UiFocusMotion m={0};ui_focus_motion_tick(&m,18,62,166,40);assert(m.x==18);
 now+=16667;ui_focus_motion_tick(&m,190,62,166,40);assert(m.x==18);
 now+=60000;ui_focus_motion_tick(&m,190,62,166,40);assert(m.x>18&&m.x<190);
 float before=m.x;now+=16667;ui_focus_motion_tick(&m,362,62,166,40);assert(m.x>=before&&m.x<190);
 /* New target every frame must move, not get stuck restarting at t=0. */
 for(int i=0;i<12;i++){before=m.x;now+=16667;ui_focus_motion_tick(&m,500+i*10,120,302,208);assert(m.x>before);}
 now+=120000;ui_focus_motion_tick(&m,610,120,302,208);assert(m.x==610&&m.y==120&&m.height==208);
 reduced=1;now++;ui_focus_motion_tick(&m,18,62,166,40);assert(m.x==18&&m.height==40);
 reduced=0;now+=1000000;ui_focus_motion_tick(&m,14,177,396,79);assert(m.x==14&&m.y==177);
 now=0;ui_focus_motion_tick(&m,50,50,50,50);assert(m.x==50);
 ui_focus_outline_draw(&m);assert(draws==4);
 ui_focus_motion_reset(&m);ui_focus_outline_draw(&m);assert(draws==4);
 for(int top=62;top<=118;top+=4)for(int bottom=448;bottom<=544;bottom++) {
  UiMediaGridLayout g=ui_media_grid_layout(top,bottom);
  assert(g.rows==2&&g.card_h>=120&&g.thumb_h>0);
  int last=top+g.step+g.card_h;
  assert(last+UI_MEDIA_GRID_FOCUS_OUTSET<bottom);
  assert(bottom-last<=UI_MEDIA_GRID_FOCUS_OUTSET+2);
  assert(g.title_y>g.thumb_h+6&&g.detail_y<g.card_h);
  top_visible=0;ui_focus_glow_draw(18,top,302,g.card_h,0,top,bottom);
  assert(clip_top==top-UI_MEDIA_GRID_FOCUS_OUTSET&&top_visible==2&&!clipped);
 }
 for(int cover=0;cover<2;cover++)for(int mode=0;mode<3;mode++){
  UiAudioLayout l=ui_audio_layout(mode,cover);
  assert(l.cover_x>=18&&l.cover_x+l.cover_size<=942);
  assert(l.cover_y+l.cover_size<=410&&l.text_x+l.text_width<=942);
  assert(l.title_y+90<UI_AUDIO_SEEK_Y);
  if(cover&&mode==VT_AUDIO_LAYOUT_LEFT)assert(l.cover_x+l.cover_size<l.text_x);
  if(cover&&mode==VT_AUDIO_LAYOUT_RIGHT)assert(l.text_x+l.text_width<l.cover_x);
 }
 puts("Crossfade focus: finite settling, rapid retarget, reduced motion, resume, four-draw budget unclipped grid borders, equal rows and audio geometry passed");
}
