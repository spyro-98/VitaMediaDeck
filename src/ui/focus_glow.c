#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
#include "ui/focus_glow.h"
#include <string.h>
#include <math.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include "settings/preferences.h"
#include "ui/theme.h"
#include "ui/theme_home.h"
#include "ui/media_grid_layout.h"

void ui_focus_motion_reset(UiFocusMotion *motion) {
	if (motion) memset(motion, 0, sizeof(*motion));
}

void ui_focus_motion_tick(UiFocusMotion *m, float x, float y, float w, float h) {
	if (!m) return;
	uint64_t now = sceKernelGetProcessTimeWide();
	float next[4] = {x,y,w,h};
	if (!m->initialized || vt_preferences_reduce_motion() || now < m->last_tick_us ||
	    now - m->last_tick_us > 250000ULL) {
		m->x=x; m->y=y; m->width=w; m->height=h;
		memcpy(m->target,next,sizeof(next));
		memcpy(m->from,next,sizeof(next));
        memset(m->velocity,0,sizeof(m->velocity));
		m->transition_us=now; m->initialized=1;
    } else if (ui_theme_id()==VT_UI_THEME_LEGACY_VITA) {
        /* Analytic critically damped motion preserves velocity on retargets,
         * remains stable at uneven frame times, and cannot accumulate frames. */
        float dt=(now-m->last_tick_us)*.000001f, omega=26, decay=expf(-omega*dt);
        float value[4]={m->x,m->y,m->width,m->height};
        for(int i=0;i<4;i++) {
            float offset=value[i]-next[i], step=(m->velocity[i]+omega*offset)*dt;
            value[i]=next[i]+(offset+step)*decay;
            m->velocity[i]=(m->velocity[i]-omega*step)*decay;
            if(fabsf(value[i]-next[i])<.02f && fabsf(m->velocity[i])<.1f) {
                value[i]=next[i];m->velocity[i]=0;
            }
        }
        m->x=value[0];m->y=value[1];m->width=value[2];m->height=value[3];
        memcpy(m->target,next,sizeof(next));
	} else {
		/* Retarget the presented rectangle, never queue inputs or ease selection.
		 * Finite cubic settle: no allocations, trig, textures or idle particles. */
		float t=(float)(now-m->transition_us)/120000.0f;
		if(t>1) t=1;
		float inverse=1-t, a=1-inverse*inverse*inverse;
		m->x=m->from[0]+(m->target[0]-m->from[0])*a;
		m->y=m->from[1]+(m->target[1]-m->from[1])*a;
		m->width=m->from[2]+(m->target[2]-m->from[2])*a;
		m->height=m->from[3]+(m->target[3]-m->from[3])*a;
		if (memcmp(next,m->target,sizeof(next))) {
			m->from[0]=m->x; m->from[1]=m->y;
			m->from[2]=m->width; m->from[3]=m->height;
			memcpy(m->target,next,sizeof(next)); m->transition_us=now;
		}
	}
	m->last_tick_us=now;
}

static unsigned focus_alpha(unsigned color,unsigned alpha) {
 return (color&0xffffffu)|((alpha>255?255:alpha)<<24);
}
void ui_theme_fx_focus(float x,float y,float w,float h) {
 if(w<8||h<8)return;
 int theme=ui_theme_id(),reduced=vt_preferences_reduce_motion();
 float phase=reduced?.5f:(float)(sceKernelGetProcessTimeWide()%4200000ULL)/4200000;
 float sweep=reduced?.5f:.5f-.5f*cosf(phase*6.28318530718f);
 int layers = vt_ui_theme_legacy_layout(theme) ? 2 : 3;
 unsigned hot=VT_THEME_FOCUS,cold=VT_THEME_COLD_LIGHT;
 /* Feathered edge geometry, not a bloom framebuffer or full-card wash. */
 for(int i=layers;i>=1;i--){
	 unsigned color =
		 focus_alpha(hot, (unsigned)(vt_ui_theme_legacy_layout(theme) ? 32 / i : 48 / i));
	 vita2d_draw_rectangle(x - i, y - i, w + i * 2, 1, color);
	 vita2d_draw_rectangle(x - i, y + h + i - 1, w + i * 2, 1, color);
	 vita2d_draw_rectangle(x - i, y - i, 1, h + i * 2, color);
	 vita2d_draw_rectangle(x + w + i - 1, y - i, 1, h + i * 2, color);
 }
 float length=fminf(w*.35f,38),head=x+4+sweep*fmaxf(0,w-length-8);
 unsigned light=focus_alpha(hot,170);
 if (vt_ui_theme_legacy_layout(theme)) {
	 vita2d_draw_rectangle(head, y - 1, length, 1, light);
	 vita2d_draw_rectangle(x + w - 4 - sweep * fmaxf(0, w - length - 8) - length, y + h - 1, length,
		                   1, light);
 } else if (theme == VT_UI_THEME_GHOST_MESH) {
	 vita2d_draw_rectangle(head, y - 1, length, 2, light);
	 vita2d_draw_rectangle(x + w - 1, y + 4 + sweep * fmaxf(0, h - 20), 2, fminf(16, h - 8),
		                   focus_alpha(cold, 160));
 } else if (theme == VT_UI_THEME_DECK_90) {
	 vita2d_draw_rectangle(x + 1, y + 3, 2, h - 6, focus_alpha(hot, 90));
	 vita2d_draw_rectangle(head, y + h - 2, length, 2, light);
 } else if (theme == VT_UI_THEME_SIGNAL) {
	 for (int i = 0; i < 5; i++) {
		 vita2d_draw_rectangle(x + w - 11 + i * 2, y - 2 + i * 2, 7, 1,
			                   focus_alpha(hot, 170 - i * 20));
		 vita2d_draw_rectangle(x + 2 + i * 2, y + h - 9 + i * 2, 7, 1,
			                   focus_alpha(cold, 100 - i * 12));
	 }
 } else if (theme == VT_UI_THEME_APERTURE) {
	 float split = fminf(w * .18f, 48) * (1 - sweep * .3f);
	 vita2d_draw_rectangle(x + w * .5f - split - 8, y - 2, split, 2, light);
	 vita2d_draw_rectangle(x + w * .5f + 8, y - 2, split, 2, light);
	 vita2d_draw_rectangle(x + 2, y + 4, 2, fminf(20, h - 8), focus_alpha(cold, 130));
 } else if (theme == VT_UI_THEME_RELAY) {
	 vita2d_draw_rectangle(head, y + h - 1, length, 2, light);
	 vita2d_draw_rectangle(x + w - 2, y + 4, 2, fminf(12, h - 8), focus_alpha(cold, 140));
	 vita2d_draw_rectangle(x + w - 13, y + 4, 12, 2, focus_alpha(cold, 140));
 } else {
	 vita2d_draw_rectangle(head, y - 1, length, 2, light);
	 vita2d_draw_rectangle(x + w - 12, y + 2, 10, 1, focus_alpha(cold, 160));
	 vita2d_draw_rectangle(x + w - 2, y + 2, 1, fminf(10, h - 4), focus_alpha(cold, 160));
 }
}

void ui_focus_outline_draw(const UiFocusMotion *m) {
	if(!m || !m->initialized || m->width<=0 || m->height<=0) return;
	float x=m->x,y=m->y,w=m->width,h=m->height;
#ifdef VMD_ENABLE_PAF_NATIVE
    int decoration=ui_native_vita_decoration(1);
#endif
    if(ui_theme_id()==VT_UI_THEME_LEGACY_VITA) { x-=2;y-=2;w+=4;h+=4; }
	ui_theme_fx_focus(x, y, w, h);
	vita2d_draw_rectangle(x,y,w,2,VT_THEME_FOCUS);
	vita2d_draw_rectangle(x,y+h-2,w,2,VT_THEME_FOCUS);
	vita2d_draw_rectangle(x,y,3,h,VT_THEME_FOCUS);
	vita2d_draw_rectangle(x+w-2,y,2,h,VT_THEME_FOCUS);
#ifdef VMD_ENABLE_PAF_NATIVE
    ui_native_vita_decoration(decoration);
#endif
}

void ui_focus_glow_draw(float x,float y,float w,float h,uint64_t now,
	int top,int bottom) {
	(void)now;
	if(w<=0 || h<=0 || bottom<=top) return;
	/* The focus sits outside the card. Its upper edge belongs to the safe
	 * gutter above the content, not to the clipped poster area. */
	int clip_top=top-UI_MEDIA_GRID_FOCUS_OUTSET;
	if(clip_top<0)clip_top=0;
	vita2d_set_clip_rectangle(0,clip_top,960,bottom);vita2d_enable_clipping();
	/* External frame above grid artwork; no full-card translucent overdraw. */
	/* The grid already owns its outer frame. Account for VMD's additional
	 * outline outset once, keeping both edges inside the reserved gutter. */
	int inset = UI_MEDIA_GRID_FOCUS_OUTSET - (ui_theme_id()==VT_UI_THEME_LEGACY_VITA ? 2 : 0);
	UiFocusMotion m={.x=x-inset,.y=y-inset,
        .width=w+2*inset,.height=h+2*inset,.initialized=1};
	ui_focus_outline_draw(&m);
	vita2d_disable_clipping();
}
