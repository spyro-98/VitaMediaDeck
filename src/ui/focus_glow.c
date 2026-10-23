#include "ui/focus_glow.h"
#include <string.h>
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
		m->transition_us=now; m->initialized=1;
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

void ui_theme_fx_focus(float x,float y,float w,float h) {
 if(ui_theme_id()==VT_UI_THEME_LEGACY)return;
 for(int i=3;i>=1;i--){
  unsigned color=(VT_THEME_FOCUS&0xffffffU)|((unsigned)(72/i)<<24);
  float left=x-i,top=y-i,width=w+i*2,height=h+i*2;
  vita2d_draw_rectangle(left,top,width,1,color);vita2d_draw_rectangle(left,top+height-1,width,1,color);
  vita2d_draw_rectangle(left,top,1,height,color);vita2d_draw_rectangle(left+width-1,top,1,height,color);
 }
 float phase=vt_preferences_reduce_motion()?0.0f:(float)(sceKernelGetProcessTimeWide()%2800000ULL)/2800000.0f;
 float length=w<80?w*.4f:32;
 vita2d_draw_rectangle(x+phase*(w-length),y-1,length,3,VT_THEME_FOCUS);
}

void ui_focus_outline_draw(const UiFocusMotion *m) {
	if(!m || !m->initialized || m->width<=0 || m->height<=0) return;
	float x=m->x,y=m->y,w=m->width,h=m->height;
    ui_theme_fx_focus(x,y,w,h);
	vita2d_draw_rectangle(x,y,w,2,VT_THEME_FOCUS);
	vita2d_draw_rectangle(x,y+h-2,w,2,VT_THEME_FOCUS);
	vita2d_draw_rectangle(x,y,3,h,VT_THEME_FOCUS);
	vita2d_draw_rectangle(x+w-2,y,2,h,VT_THEME_FOCUS);
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
	UiFocusMotion m={.x=x-UI_MEDIA_GRID_FOCUS_OUTSET,.y=y-UI_MEDIA_GRID_FOCUS_OUTSET,
        .width=w+2*UI_MEDIA_GRID_FOCUS_OUTSET,.height=h+2*UI_MEDIA_GRID_FOCUS_OUTSET,.initialized=1};
	ui_focus_outline_draw(&m);
	vita2d_disable_clipping();
}
