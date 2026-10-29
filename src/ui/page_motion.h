#ifndef VMD_PAGE_MOTION_H
#define VMD_PAGE_MOTION_H
#include <math.h>
#include "settings/preferences.h"
#include "ui/scroll_view.h"
#include "ui/touch.h"
#include <psp2/ctrl.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
/* Pure, bounded viewport physics. Selection and data are never delayed. */
typedef struct {
    float row, velocity, camera, camera_velocity;
    uint64_t context, last;
    int ready, theme, columns, identity_length;
    float step,viewport_pixels;
    UiScrollView pixels;
    uint64_t input_last,owner;
    int direct,count,slots;
} UiPageMotion;
typedef struct {
    int first, end;
    float dx, dy, energy;
} UiPageView;
typedef struct {float scroll_seconds,menu_us;int menu_from_right;} UiThemeMotionProfile;
static inline UiThemeMotionProfile ui_theme_motion_profile(int theme) {
    /* VMD inherits the Legacy motion profile at stable ID 7. */
    static const UiThemeMotionProfile profiles[8] = {
        {.17f, 60000, 0}, {.22f, 70000, 1}, {.16f, 42000, 0}, {.19f, 50000, 1},
        {.25f, 85000, 0}, {.20f, 60000, 0}, {.23f, 80000, 1}, {.17f, 60000, 0}};
    return profiles[theme >= 0 && theme < 8 ? theme : 0];
}
static inline void ui_page_rebase(UiPageMotion *motion,int before,int after) {
    if(motion->ready && motion->columns>0)
        motion->row+=(float)(after/motion->columns-before/motion->columns);
}
static inline uint64_t ui_page_key(const char *owner, const char *identity) {
    uint64_t key = 1469598103934665603ULL;
    const char *parts[2] = {owner, identity};
    for (int i = 0; i < 2; i++) {
        if (parts[i]) for (const unsigned char *p = (const unsigned char *)parts[i]; *p; p++) {
            key ^= *p; key *= 1099511628211ULL;
        }
        key ^= 255; key *= 1099511628211ULL;
    }
    return key;
}
static inline void ui_page_spring(float *position, float *velocity, float target,
                                  float dt, float omega) {
    float delta = *position - target;
    float decay = expf(-omega * dt), impulse = *velocity + omega * delta;
    *position = target + (delta + impulse * dt) * decay;
    *velocity = (*velocity - omega * impulse * dt) * decay;
    if (fabsf(*position-target) < .0005f && fabsf(*velocity) < .005f) {
        *position = target; *velocity = 0;
    }
}
static inline UiPageView ui_page_current_view(UiPageMotion *m,int first,int count,int columns,int slots,float step){
    float target=(float)(first/columns);
    float rows=m->viewport_pixels>0?m->viewport_pixels/step:(float)((slots+columns-1)/columns);
    if(m->theme==6&&rows<=1.01f)rows=1;
    float last_row=count?(float)((count-1)/columns):0;
    if(m->row>last_row){m->row=last_row;m->velocity=0;}
    UiPageView view={0};
    view.first=(int)floorf(m->row)*columns;
    view.end=(int)ceilf(m->row+rows)*columns;
    if(view.end>count)view.end=count;
    if(view.first>view.end)view.first=view.end;
    view.dy=(target-m->row)*step;
    float camera=m->camera;
    view.dx = camera * (vt_ui_theme_legacy_layout(m->theme) ? .35f
                        : m->theme == 2                     ? 1.4f
                        : m->theme == 3                     ? -.9f
                                                         : 1);
    if(m->theme==1)view.dy+=camera*.35f;
    if(m->theme==4)view.dy+=camera*.5f;
    if(m->theme==5)view.dy-=camera*.25f;
    if(m->theme==6){
        if(rows==1){view.dx+=(target-m->row)*960;view.dy=0;}
        view.dy+=camera*.3f;
    }
    view.energy=fminf(1,fabsf(m->velocity)*.05f+fabsf(camera)*.15f);
    return view;
}
static inline UiPageView ui_page_update(UiPageMotion *m, uint64_t now, int theme,
    int reduced, const char *owner, const char *identity, int first, int count,
    int columns, int slots, float step) {
    if (columns < 1) columns = 1;
    if (slots < columns) slots = columns;
    if (count < 0) count = 0;
    if (first < 0) first = 0;
    if (first >= count) first = count ? (count-1)/columns*columns : 0;
    if (!isfinite(step) || step < 1) step = 1;
    float target = (float)(first/columns);
    float rows = (float)((slots+columns-1)/columns);
    uint64_t context = ui_page_key(owner, identity);
    int identity_length = identity ? (int)strlen(identity) : 0;
    int reset = !m->ready || m->theme != theme || m->columns != columns ||
        m->step != step || now < m->last || now-m->last > 250000ULL || (reduced&&!m->direct);
    if (reset) {
        *m = (UiPageMotion){.row=target, .context=context, .theme=theme,
            .columns=columns, .step=step, .identity_length=identity_length, .ready=1};
    } else {
        float dt = (float)(now-m->last)/1000000.0f;
        float duration=ui_theme_motion_profile(theme).scroll_seconds;
        if (m->context != context) {
            m->direct=0;m->pixels.ready=0;
            /* New data shares the same stable surface; no per-item replay.
             * Kick velocity, never position: quick reversals stay continuous. */
            m->row=target; m->velocity=0;
            m->camera_velocity=identity_length<m->identity_length?-400:400;
            m->context=context; m->identity_length=identity_length;
        }
        /* At most a visible page of travel; a held key cannot create a backlog. */
        float limit=rows>1?rows:1;
        if(m->row<target-limit){m->row=target-limit;m->velocity=0;}
        if(m->row>target+limit){m->row=target+limit;m->velocity=0;}
        if(!m->direct)ui_page_spring(&m->row,&m->velocity,target,dt,8/duration);
        ui_page_spring(&m->camera,&m->camera_velocity,0,dt,24);
        if(m->row<0){m->row=0;m->velocity=0;}
        if(m->camera>12){m->camera=12;m->camera_velocity=fminf(0,m->camera_velocity);}
        if(m->camera< -12){m->camera=-12;m->camera_velocity=fmaxf(0,m->camera_velocity);}
    }
    m->last=now;m->owner=ui_page_key(owner,NULL);m->count=count;m->slots=slots;
    return ui_page_current_view(m,first,count,columns,slots,step);
}
/* Same input contract for every themed list/grid. first_rows describes
 * legacy browsers whose top is stored as a row instead of an item index. */
static inline int ui_page_pixel_input(UiPageMotion *m,uint64_t now,int reduced,const char *owner,
 int x,int y,int width,int height,int count,int first_rows,int *first,int *selected,
 unsigned buttons,unsigned char lx,unsigned char ly,unsigned *flags,const UiTouchEvent *touch){
 if(!m->ready||m->owner!=ui_page_key(owner,NULL)||m->theme<0||m->count!=count||!count||height<=0)return 0;
 int columns=m->columns;float logical_rows=(float)((m->slots+columns-1)/columns);
 int horizontal=m->theme==6&&logical_rows==1;
 float unit=horizontal?960:m->step,viewport=horizontal?960:height;
 m->viewport_pixels=horizontal?m->step:height;
 if(!m->pixels.ready){ui_scroll_reset(&m->pixels,m->row*unit);m->pixels.ready=1;m->pixels.last=now;}
 ui_scroll_bounds(&m->pixels,((count+columns-1)/columns)*unit,viewport);
 float dt=m->input_last&&now>m->input_last?(now-m->input_last)/1000000.f:1.f/60;
 if(dt>.05f)dt=.05f;
 m->input_last=now;
 if(buttons&(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT)){m->direct=0;m->pixels.dragging=m->pixels.coasting=0;}
 int inside=touch->down_x>=x&&touch->down_x<x+width&&touch->down_y>=y&&touch->down_y<y+height;
 float at=horizontal?touch->x:touch->y,down=horizontal?touch->down_x:touch->down_y;
 int claimed=0;
 if((*flags&UI_TOUCH_EVENT_DOWN)&&inside){m->direct=1;ui_scroll_drag_begin(&m->pixels,at);claimed=1;}
 if(m->pixels.dragging&&(*flags&(UI_TOUCH_EVENT_MOVE|UI_TOUCH_EVENT_HOLD|UI_TOUCH_EVENT_UP))){
  m->direct=1;ui_scroll_drag_move(&m->pixels,down,at,dt);claimed=1;
  if(*flags&UI_TOUCH_EVENT_UP)ui_scroll_drag_end(&m->pixels,reduced);
 }
 if(inside&&claimed&&!(*flags&UI_TOUCH_EVENT_TAP))*flags=0;
 int axis=horizontal?(abs((int)lx-128)>32?lx:ly):ly;
 if(abs(axis-128)>32){m->direct=1;ui_scroll_stick(&m->pixels,axis,dt);}
 if(m->direct){
  ui_scroll_tick(&m->pixels,now,reduced);m->row=m->pixels.position/unit;m->velocity=m->pixels.velocity/unit;
  int beginning=(int)floorf(m->row)*columns,end=(int)ceilf((m->pixels.position+viewport)/unit)*columns;
  *first=first_rows?beginning/columns:beginning;
  int column=*selected%columns;if(*selected<beginning)*selected=beginning+column;
  if(*selected>=end)*selected=end-columns+column;
  if(*selected>=count)*selected=count-1;
  if(*selected<0)*selected=0;
 }else {m->pixels.position=m->row*unit;m->pixels.target=m->pixels.position;}
 m->last=now;return claimed||m->direct;
}
#endif
