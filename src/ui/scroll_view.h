#ifndef VMD_UI_SCROLL_VIEW_H
#define VMD_UI_SCROLL_VIEW_H
#include <stdint.h>
#include <math.h>
/* Pixel viewport shared by keyboard focus, a held stick and direct touch.
 * No page replacement: the same absolute item coordinates move continuously. */
typedef struct {
 float position,target,velocity,maximum,down_position,last_y;
 uint64_t last;
 int ready,dragging,coasting;
} UiScrollView;
static inline float ui_scroll_clamp(float v,float maximum){return v<0?0:v>maximum?maximum:v;}
static inline void ui_scroll_reset(UiScrollView *s,float offset){
 *s=(UiScrollView){.position=offset,.target=offset};
}
static inline void ui_scroll_bounds(UiScrollView *s,float content,float viewport){
 s->maximum=fmaxf(0,content-viewport);s->target=ui_scroll_clamp(s->target,s->maximum);
 s->position=ui_scroll_clamp(s->position,s->maximum);
}
static inline void ui_scroll_reveal(UiScrollView *s,float begin,float end,float viewport){
 float target=s->target;
 if(begin<target)target=begin;
 if(end>target+viewport)target=end-viewport;
 s->target=ui_scroll_clamp(target,s->maximum);s->coasting=0;
}
static inline void ui_scroll_tick(UiScrollView *s,uint64_t now,int reduced){
 if(!s->ready){s->position=ui_scroll_clamp(s->target,s->maximum);s->ready=1;s->last=now;return;}
 float dt=now>s->last?(now-s->last)/1000000.f:0;s->last=now;
 if(dt>.1f)dt=.1f;
 if(s->dragging)return;
 if(s->coasting){
  float decay=expf(-7*dt);s->position=ui_scroll_clamp(s->position+s->velocity*(1-decay)/7,s->maximum);
  s->velocity*=decay;s->target=s->position;
  if(fabsf(s->velocity)<8||s->position==0||s->position==s->maximum){s->coasting=0;s->velocity=0;}
 }else if(reduced){s->position=s->target;s->velocity=0;}
 else {
  float delta=s->position-s->target,decay=expf(-36*dt),impulse=s->velocity+36*delta;
  s->position=s->target+(delta+impulse*dt)*decay;s->velocity=(s->velocity-36*impulse*dt)*decay;
  s->position=ui_scroll_clamp(s->position,s->maximum);
  if(fabsf(s->position-s->target)<.1f&&fabsf(s->velocity)<1){s->position=s->target;s->velocity=0;}
 }
}
static inline void ui_scroll_drag_begin(UiScrollView *s,float y){
 s->dragging=1;s->coasting=0;s->velocity=0;s->down_position=s->position;s->last_y=y;
}
static inline void ui_scroll_drag_move(UiScrollView *s,float down_y,float y,float dt){
 if(!s->dragging)return;
 float before=s->position;s->position=ui_scroll_clamp(s->down_position+down_y-y,s->maximum);s->target=s->position;
 if(dt>0&&y!=s->last_y){float speed=(s->position-before)/dt;s->velocity=s->velocity*.35f+speed*.65f;}
 if(dt>0&&y==s->last_y)s->velocity*=expf(-18*dt);
 s->last_y=y;
}
static inline void ui_scroll_drag_end(UiScrollView *s,int reduced){
 s->dragging=0;s->coasting=!reduced&&fabsf(s->velocity)>8;s->target=s->position;if(reduced)s->velocity=0;
}
static inline int ui_scroll_stick(UiScrollView *s,int axis,float dt){
 float amount=(axis-128)/127.f;if(fabsf(amount)<.24f)return 0;
 amount=copysignf((fabsf(amount)-.24f)/.76f,amount);
 s->coasting=0;s->dragging=0;s->velocity=0;
 s->position=ui_scroll_clamp(s->position+amount*700*dt,s->maximum);s->target=s->position;return 1;
}
#endif
