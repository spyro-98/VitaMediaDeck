#ifndef VMD_PAGE_MOTION_H
#define VMD_PAGE_MOTION_H
#include <math.h>
#include "settings/preferences.h"
#include <stdint.h>
#include <string.h>
/* Pure, bounded viewport physics. Selection and data are never delayed. */
typedef struct {
    float row, velocity, camera, camera_velocity;
    uint64_t context, last;
    int ready, theme, columns, identity_length;
    float step;
} UiPageMotion;
typedef struct {
    int first, end;
    float dx, dy, energy;
} UiPageView;
typedef struct {float scroll_seconds,menu_us;int menu_from_right;} UiThemeMotionProfile;
static inline UiThemeMotionProfile ui_theme_motion_profile(int theme) {
    /* Legacy Vita inherits the Legacy motion profile at stable ID 7. */
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
static inline UiPageView ui_page_update(UiPageMotion *m, uint64_t now, int theme,
    int reduced, const char *owner, const char *identity, int first, int count,
    int columns, int slots, float step) {
    UiPageView view = {0};
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
        m->step != step || now < m->last || now-m->last > 250000ULL || reduced;
    if (reset) {
        *m = (UiPageMotion){.row=target, .context=context, .theme=theme,
            .columns=columns, .step=step, .identity_length=identity_length, .ready=1};
    } else {
        float dt = (float)(now-m->last)/1000000.0f;
        float duration=ui_theme_motion_profile(theme).scroll_seconds;
        if (m->context != context) {
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
        ui_page_spring(&m->row,&m->velocity,target,dt,8/duration);
        ui_page_spring(&m->camera,&m->camera_velocity,0,dt,24);
        if(m->row<0){m->row=0;m->velocity=0;}
        if(m->camera>12){m->camera=12;m->camera_velocity=fminf(0,m->camera_velocity);}
        if(m->camera< -12){m->camera=-12;m->camera_velocity=fmaxf(0,m->camera_velocity);}
    }
    float last_row=count?(float)((count-1)/columns):0;
    if(m->row>last_row){m->row=last_row;m->velocity=0;}
    m->last=now;
    view.first=(int)floorf(m->row)*columns;
    view.end=(int)ceilf(m->row+rows)*columns;
    if(view.end>count)view.end=count;
    if(view.first>view.end)view.first=view.end;
    view.dy=(target-m->row)*step;
    float camera=m->camera;
    view.dx = camera * (vt_ui_theme_legacy_layout(theme) ? .35f
                        : theme == 2                     ? 1.4f
                        : theme == 3                     ? -.9f
                                                         : 1);
    if(theme==1)view.dy+=camera*.35f;
    if(theme==4)view.dy+=camera*.5f;
    if(theme==5)view.dy-=camera*.25f;
    if(theme==6){
        if(rows==1){view.dx+=(target-m->row)*960;view.dy=0;}
        view.dy+=camera*.3f;
    }
    view.energy=fminf(1,fabsf(m->velocity)*.05f+fabsf(camera)*.15f);
    return view;
}
#endif
