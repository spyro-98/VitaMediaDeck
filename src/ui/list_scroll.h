#ifndef VMD_UI_LIST_SCROLL_H
#define VMD_UI_LIST_SCROLL_H
#include "ui/scroll_view.h"
#include "ui/touch.h"
#include "settings/preferences.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
/* Row lists and text readers share the same direct-touch pixel viewport. */
static inline void ui_list_scroll_input(UiScrollView *s,int count,float step,
 int x,int y,int width,int height,unsigned nav,unsigned held,int ly,
 unsigned *flags,const UiTouchEvent *touch,int *selected){
 uint64_t now=sceKernelGetProcessTimeWide();
 float dt=s->last&&now>s->last?(now-s->last)/1000000.f:1.f/60;
 if(dt>.05f)dt=.05f;
 ui_scroll_bounds(s,count*step,height);
 if(nav&(SCE_CTRL_UP|SCE_CTRL_DOWN)){
  s->dragging=s->coasting=0;
  if(selected)ui_scroll_reveal(s,*selected*step,(*selected+1)*step,height);
  else {s->target+=nav&SCE_CTRL_DOWN?step:-step;s->target=ui_scroll_clamp(s->target,s->maximum);}
 }
 int inside=touch->down_x>=x&&touch->down_x<x+width&&touch->down_y>=y&&touch->down_y<y+height;
 if((*flags&UI_TOUCH_EVENT_DOWN)&&inside)ui_scroll_drag_begin(s,touch->y);
 if(s->dragging&&(*flags&(UI_TOUCH_EVENT_MOVE|UI_TOUCH_EVENT_HOLD|UI_TOUCH_EVENT_UP))){
  ui_scroll_drag_move(s,touch->down_y,touch->y,dt);
  if(*flags&UI_TOUCH_EVENT_UP)ui_scroll_drag_end(s,vt_preferences_reduce_motion());
  if(!(*flags&UI_TOUCH_EVENT_TAP))*flags=0;
 }
 if(!(held&(SCE_CTRL_UP|SCE_CTRL_DOWN)))ui_scroll_stick(s,ly,dt);
 ui_scroll_tick(s,now,vt_preferences_reduce_motion());
 if(selected&&(s->dragging||s->coasting||ly<96||ly>160)){
  int first=(int)ceilf(s->position/step),last=(int)floorf((s->position+height)/step)-1;
  if(last<first)last=first;
  if(*selected<first)*selected=first;
  if(*selected>last)*selected=last;
  if(*selected>=count)*selected=count?count-1:0;
 }
}
static inline int ui_list_scroll_first(const UiScrollView *s,float step){return (int)floorf(s->position/step);}
static inline int ui_list_scroll_end(const UiScrollView *s,float step,int height,int count){
 int end=(int)ceilf((s->position+height)/step);return end<count?end:count;
}
#endif
