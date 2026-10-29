#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
#include "ui/components.h"
#include "ui/page_motion.h"
#include "ui/list_scroll.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>

#include "settings/preferences.h"
#include "history/playback_history.h"
#include "ui/brand.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "ui/theme_home.h"
#include "i18n/i18n.h"

#ifdef VMD_ENABLE_PAF_NATIVE
static void rounded_backing(float x, float y, float w, float h, unsigned color);
#endif

const char *ui_menu_group_label(int group){
 static const char *const en[]={"","Playback","Library","View","Connections","Manage","Information"};
 static const char *const it[]={"","Riproduzione","Raccolta","Visualizzazione","Collegamenti","Gestione","Informazioni"};
 if(group<1||group>UI_MENU_GROUP_INFO)return "";
 return vt_i18n_text(en[group],it[group]);
}
void ui_context_menu_category(UiContextAction *items,int count,int action,int group){
 for(int i=0;i<count;i++)if(items[i].action==action)items[i].enabled=(items[i].enabled&1)|(group<<8);
}
void ui_menu_category(float x,float y,float width,int group){
 ui_menu_text(x,y+25,width,ui_menu_group_label(group),UI_MENU_ACCENT,UI_FONT_SMALL);
}
static unsigned context_groups(const UiContextAction *items,int count){
 unsigned starts=0;for(int i=0;i<count&&i<32;i++){
  int group=items[i].enabled>>8;
  if(group&&(!i||(items[i-1].enabled>>8)!=group))starts|=1u<<i;
 }return starts;
}
int ui_context_menu_input(UiContextMenu *menu,const UiContextAction *items,int count,
 unsigned *pressed,unsigned held,unsigned char ly,unsigned *touches,const UiTouchEvent *touch){
 menu->group_starts=context_groups(items,count);
 if(*pressed&SCE_CTRL_LTRIGGER){menu->open=0;return -1;}
 if(*pressed&SCE_CTRL_RTRIGGER){menu->open=!menu->open;menu->cursor=menu->top=0;ui_scroll_reset(&menu->viewport,0);ui_nav_repeat_reset(&menu->repeat);*pressed=*touches=0;return -1;}
 if(!menu->open)return -1;
 if(menu->cursor>=count)menu->cursor=count?count-1:0;
 unsigned nav=ui_nav_repeat_update(&menu->repeat,*pressed,held,128,128,SCE_CTRL_UP|SCE_CTRL_DOWN);
 if((nav&SCE_CTRL_UP)&&menu->cursor>0)menu->cursor--;
 if((nav&SCE_CTRL_DOWN)&&menu->cursor+1<count)menu->cursor++;
 UiScrollView *s=&menu->viewport;unsigned groups=menu->group_starts;
 float extent=count?ui_menu_group_y(count-1,0,52,groups)+52:0;
 uint64_t now=sceKernelGetProcessTimeWide();float dt=s->last&&now>s->last?(now-s->last)*.000001f:1.f/60;if(dt>.05f)dt=.05f;
 ui_scroll_bounds(s,extent,416);
 if(nav){s->dragging=s->coasting=0;float y=ui_menu_group_y(menu->cursor,0,52,groups);ui_scroll_reveal(s,y-(ui_menu_group_start(groups,menu->cursor)?UI_MENU_HEADING_HEIGHT:0),y+52,416);}
 int inside=touch->down_x>=548&&touch->down_x<942&&touch->down_y>=94&&touch->down_y<510;
 if((*touches&UI_TOUCH_EVENT_DOWN)&&inside)ui_scroll_drag_begin(s,touch->y);
 if(s->dragging&&(*touches&(UI_TOUCH_EVENT_MOVE|UI_TOUCH_EVENT_HOLD|UI_TOUCH_EVENT_UP))){
  ui_scroll_drag_move(s,touch->down_y,touch->y,dt);
  if(*touches&UI_TOUCH_EVENT_UP)ui_scroll_drag_end(s,vt_preferences_reduce_motion());
  if(!(*touches&UI_TOUCH_EVENT_TAP))*touches=0;
 }
 if(!(held&(SCE_CTRL_UP|SCE_CTRL_DOWN)))ui_scroll_stick(s,ly,dt);
 ui_scroll_tick(s,now,vt_preferences_reduce_motion());
 menu->top=0;while(menu->top<count&&ui_menu_group_y(menu->top,0,52,groups)+52<=s->position)menu->top++;
 if(count&&(s->dragging||s->coasting||ly<96||ly>160)){
  int first=menu->top,last=first;
  for(int i=first;i<count;i++){float y=ui_menu_group_y(i,0,52,groups);if(y+52<=s->position+416)last=i;}
  if(menu->cursor<first)menu->cursor=first;
  if(menu->cursor>last)menu->cursor=last;
 }
 int action=-1;
 if((*pressed&SCE_CTRL_CROSS)&&count&&(items[menu->cursor].enabled&1))action=items[menu->cursor].action;
 if(*pressed&SCE_CTRL_CIRCLE)menu->open=0;
 if((*touches&UI_TOUCH_EVENT_TAP)&&menu->animation>.99f){
  if(touch->x<530||ui_touch_hit_rect(touch->x,touch->y,878,12,64,40))menu->open=0;
  for(int i=menu->top;i<count;i++){
   float y=ui_menu_group_y(i,94,52,groups)-s->position;
   if(y>=510)break;
   if(touch->y>=94&&touch->y<510&&ui_touch_hit_rect(touch->x,touch->y,548,y,394,52)){
    menu->cursor=i;if(items[i].enabled&1)action=items[i].action;
   }
  }
 }
 *pressed=*touches=0;if(action>=0)menu->open=0;return action;
}

void ui_context_menu_tick(UiContextMenu *menu) {
	uint64_t now=sceKernelGetProcessTimeWide();
	float elapsed=menu->last_us && now>menu->last_us?(float)(now-menu->last_us):16667.0f;
	menu->last_us=now;
	float alpha=elapsed/(ui_theme_motion_profile(ui_theme_id()).menu_us+10000+elapsed);
	menu->animation+=((menu->open?1.0f:0.0f)-menu->animation)*alpha;
	if (vt_preferences_reduce_motion() || fabsf(menu->animation-(menu->open?1.0f:0.0f))<.025f)
		menu->animation=menu->open?1.0f:0.0f;
	if(menu->open) ui_focus_motion_tick(&menu->focus,18,ui_menu_group_y(menu->cursor,94,52,menu->group_starts)-menu->viewport.position,394,46);
	else ui_focus_motion_reset(&menu->focus);
}
void ui_context_menu_draw(const UiContextMenu *menu, const char *title,
	const UiContextAction *items, int count) {
	if (menu->animation<=.01f) return;
	#ifdef VMD_ENABLE_PAF_NATIVE
    int native_scope = ui_native_vita_scope(UI_NATIVE_SCOPE_ACTIONS);
#endif
    float x=960-430*menu->animation;
	vita2d_draw_rectangle(0,0,x,544,RGBA8(0,0,0,(unsigned)(160*menu->animation)));
	ui_menu_surface(x,0,430,544,1);
	vita2d_draw_rectangle(x,0,2,544,VT_THEME_SIGNAL);
	vita2d_font *font=ui_runtime_font(UI_FONT_SMALL);
	if(!font){
#ifdef VMD_ENABLE_PAF_NATIVE
      ui_native_vita_scope(native_scope);
#endif
      return;
    }
	float ox=x-530;
	vita2d_set_clip_rectangle((int)x,0,960,544);
	vita2d_enable_clipping();
	char fitted[192];
	ui_font_fit_text(font,UI_FONT_SMALL,title,fitted,sizeof(fitted),250);
	ui_menu_text(ox+548,38,250,fitted,UI_MENU_TEXT,UI_FONT_SMALL);
	ui_action_button(ox+878,12,64,40,UI_MENU_BG,"R1","",0);
	ui_menu_text(ox+548,76,394,vt_i18n_str(VT_STR_UI_CONTEXT_HINT),UI_MENU_MUTED,UI_FONT_SMALL);
	vita2d_set_clip_rectangle((int)x,94,960,510);
    if(menu->open){UiFocusMotion f=menu->focus;f.x+=x;ui_menu_focus_draw(&f);}
    unsigned groups=context_groups(items,count);
    for (int i=menu->top;i<count;i++) {
        float row_y=ui_menu_group_y(i,94,52,groups)-menu->viewport.position;
        if(row_y-UI_MENU_HEADING_HEIGHT>=510)break;
        int group=items[i].enabled>>8;
        int heading=group&&(i==0||(items[i-1].enabled>>8)!=group);
        if(heading)ui_menu_category(ox+562,row_y-UI_MENU_HEADING_HEIGHT,366,group);
        ui_menu_row(ox+548,row_y,394,52,menu->open&&i==menu->cursor,1);
		ui_font_fit_text(font,UI_FONT_SMALL,items[i].label,fitted,sizeof(fitted),366);
		ui_menu_text(ox+562,row_y+(items[i].detail&&items[i].detail[0]?19:29),366,fitted,
		    (items[i].enabled&1)?UI_MENU_TEXT:UI_MENU_FAINT,UI_FONT_SMALL);
		if(items[i].detail && items[i].detail[0]) {
			ui_font_fit_text(font,UI_FONT_SMALL,items[i].detail,fitted,sizeof(fitted),366);
			ui_menu_text(ox+562,row_y+39,366,fitted,UI_MENU_MUTED,UI_FONT_SMALL);
		}
	}
	vita2d_disable_clipping();
#ifdef VMD_ENABLE_PAF_NATIVE
    ui_native_vita_scope(native_scope);
#endif
}
/* Fixed 136x108 cell, including margins; both browser grids share this glyph. */
void ui_folder_icon(float x,float y) {
#ifdef VMD_ENABLE_PAF_NATIVE
 if (ui_native_vita_active()) {
  rounded_backing(x+20,y+24,62,20,VT_THEME_BLUE_LIGHT);
  rounded_backing(x+12,y+35,112,65,VT_THEME_BLUE);
  ui_native_vita_panel(x+12,y+35,112,65,VT_THEME_BLUE,0);
  return;
 }
#endif
 vita2d_draw_rectangle(x+20,y+24,62,15,VT_THEME_BLUE_LIGHT);
 vita2d_draw_rectangle(x+12,y+35,112,65,VT_THEME_BLUE);
 vita2d_draw_rectangle(x+12,y+35,112,4,VT_THEME_BLUE_BRIGHT);
}
void ui_artwork_tile(float x,float y,float size,vita2d_texture *texture,const char *placeholder) {
	vita2d_draw_rectangle(x,y,size,size,VT_THEME_BG);
	if (texture) {
		float w=vita2d_texture_get_width(texture), h=vita2d_texture_get_height(texture);
		if(w>0 && h>0) {
			float scale=size/(w>h?w:h);
			vita2d_draw_texture_scale(texture,x+(size-w*scale)*.5f,y+(size-h*scale)*.5f,scale,scale);
		}
	} else {
		vita2d_draw_rectangle(x,y,size,1,VT_THEME_BORDER);
		vita2d_draw_rectangle(x,y+size-1,size,1,VT_THEME_BORDER_DIM);
		vita2d_font *font=ui_runtime_font(UI_FONT_SMALL);
		if(font && placeholder) {
			int width=ui_font_text_width(font,UI_FONT_SMALL,placeholder);
			ui_font_draw_text(font,x+(size-width)*.5f,y+size*.5f+6,VT_THEME_TEXT_FAINT,UI_FONT_SMALL,placeholder);
		}
	}
}
void ui_scroll_rail(float x,float y,float height,int first,int visible,int count) {
	if(count<=visible || visible<=0 || height<=0) return;
	float thumb=height*visible/count;
	if(thumb<16)thumb=16;
	float offset=(height-thumb)*first/(count-visible);
	if(offset<0)offset=0;
	if(offset>height-thumb)offset=height-thumb;
	vita2d_draw_rectangle(x,y,2,height,VT_THEME_BORDER_DIM);
	vita2d_draw_rectangle(x,y+offset,2,thumb,VT_THEME_SIGNAL_BRIGHT);
}

static unsigned int channel(unsigned int color, unsigned int shift) {
	return (color >> shift) & 0xffU;
}

void ui_text_line(float x,float baseline,float width,const char *text,unsigned color,unsigned size) {
	vita2d_font *font=ui_runtime_font(size);
	if(!font || !text || width<=0)return;
	char fitted[512];ui_font_fit_text(font,size,text,fitted,sizeof(fitted),(int)width);
#ifdef VMD_ENABLE_PAF_NATIVE
    /* Keep the caller's available width, not the smaller fitted glyph advance.
     * One deliberate fit is enough; PAF must not ellipsize that result again. */
    if (!ui_input_prompt_contains(fitted) &&
        ui_native_vita_text(x, baseline, width, size, color, fitted)) return;
#endif
	ui_font_draw_text(font,(int)x,(int)baseline,color,size,fitted);
}

void ui_tab(float x,float y,float w,float h,const char *label,int active){
 ui_menu_text(x+14,y+h*.5f+7,w-28,label,active?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 if(active)vita2d_draw_rectangle(x+14,y+h-2,w-28,2,VT_THEME_SIGNAL_BRIGHT);
}

void ui_footer(int bottom,const char *left,const char *right){
 if(bottom<40)return;
 vita2d_draw_rectangle(18,bottom-36,924,1,VT_THEME_BORDER_DIM);
 ui_menu_text(28,bottom-11,554,left,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 vita2d_font *font=ui_runtime_font(UI_FONT_SMALL);
 if(font&&right){char fitted[192];int width=ui_font_fit_text(font,UI_FONT_SMALL,right,fitted,sizeof(fitted),330);
  ui_menu_text(932-width,bottom-11,330,fitted,VT_THEME_TEXT,UI_FONT_SMALL);}
}
void ui_modal_panel(float x,float y,float w,float h){
 vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,194));ui_menu_surface(x,y,w,h,1);
}

void ui_text_block(float x,float baseline,float width,int line_height,int lines,const char *text,unsigned color,unsigned size) {
	vita2d_font *font=ui_runtime_font(size);
	if(!font || !text || width<=0)return;
	for(int row=0;row<lines && *text;row++) {
		char line[512];size_t n=0,fit=0,space=0;
		while(text[n] && text[n]!='\n' && n<sizeof(line)-5) {
			size_t end=n+1;
			while(text[end] && ((unsigned char)text[end]&0xc0)==0x80)end++;
			if(end>=sizeof(line))break;
			memcpy(line,text,end);line[end]=0;
			if(ui_font_text_width(font,size,line)>width)break;
			if(text[n]==' ')space=n;
			fit=end;n=end;
		}
		if(!fit)break;
		if(text[n] && text[n]!='\n' && space)fit=space;
		memcpy(line,text,fit);line[fit]=0;
		ui_font_draw_text(font,x,baseline+row*line_height,color,size,line);
		text+=fit;while(*text==' ')text++;
		if(*text=='\n')text++;
	}
}

unsigned int ui_contrast_bw(unsigned int background) {
	unsigned int red = channel(background, 0);
	unsigned int green = channel(background, 8);
	unsigned int blue = channel(background, 16);
	/* Integer approximation of relative luminance; 142 is a slightly
	 * conservative threshold for small system-PGF text on the Vita OLED. */
	unsigned int luminance = (red * 299U + green * 587U + blue * 114U) / 1000U;
	return luminance >= 142U ? RGBA8(0, 0, 0, 255)
	                         : RGBA8(255, 255, 255, 255);
}

/* Keep the content area clear: decoration must not reserve a second gutter. */
void ui_chrome_background(unsigned int base, unsigned int accent) {
	(void)accent;
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT, base);
    ui_theme_fx_background(base);
}

void ui_scene_identity(float x, float y, float width, const char *code,
	                   const char *title, const char *detail) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	/* Breadcrumbs identify the section without displacing its real title. */
	(void)code;
	if (body && title && title[0]) {
		char fitted[192];
		ui_font_fit_text(body, UI_FONT_BODY, title, fitted, sizeof(fitted),
		                 (int)width);
		ui_font_draw_text(body, (int)x, (int)y + 22,
		                  VT_THEME_TEXT, UI_FONT_BODY, fitted);
	}
	if (small && detail && detail[0]) {
		char fitted[256];
		ui_font_fit_text(small, UI_FONT_SMALL, detail, fitted, sizeof(fitted),
		                 (int)width);
		ui_font_draw_text(small, (int)x, (int)y + 42,
		                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
	}
}

#ifdef VMD_ENABLE_PAF_NATIVE
static void rounded_backing(float x, float y, float w, float h, unsigned color) {
    /* One bounded GPU batch, no heap allocation or per-corner draw calls. The
     * firmware nine-slice owns the finish; this base serves media cutouts. */
    static const float arc[7][2] = {
        {0,1}, {.258819f,.965926f}, {.5f,.866025f}, {.707107f,.707107f},
        {.866025f,.5f}, {.965926f,.258819f}, {1,0}
    };
    float radius = fminf(10, fminf(w, h) * .25f);
    vita2d_color_vertex ring[28], triangles[84];
    for (int corner = 0; corner < 4; corner++) {
        float cx = corner < 2 ? x + w - radius : x + radius;
        float cy = corner == 0 || corner == 3 ? y + radius : y + h - radius;
        for (int i = 0; i < 7; i++) {
            float sx = arc[i][0], sy = arc[i][1];
            float dx = corner == 0 ? sx : corner == 1 ? sy : corner == 2 ? -sx : -sy;
            float dy = corner == 0 ? -sy : corner == 1 ? sx : corner == 2 ? sy : -sx;
            ring[corner * 7 + i] = (vita2d_color_vertex){cx + dx * radius, cy + dy * radius, .5f, color};
        }
    }
    for (int i = 0; i < 28; i++) {
        triangles[i * 3] = (vita2d_color_vertex){x + w * .5f, y + h * .5f, .5f, color};
        triangles[i * 3 + 1] = ring[i];
        triangles[i * 3 + 2] = ring[(i + 1) % 28];
    }
    ui_native_vita_cover(x, y, w, h, color);
#ifdef __vita__
    vita2d_color_vertex *gpu = vita2d_pool_memalign(sizeof(triangles), 16);
    if (!gpu) {
        vita2d_draw_rectangle(x + radius, y, w - radius * 2, h, color);
        return;
    }
    memcpy(gpu, triangles, sizeof(triangles));
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, gpu, 84);
#else
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, triangles, 84);
#endif
}
#endif

void ui_panel(float x, float y, float width, float height,
	          unsigned int fill, unsigned int accent, int focused) {
#ifdef VMD_ENABLE_PAF_NATIVE
    if (ui_native_vita_active()) rounded_backing(x, y, width, height, fill);
    else
#endif
    vita2d_draw_rectangle(x, y, width, height, fill);
#ifdef VMD_ENABLE_PAF_NATIVE
    /* Retain the base under media cutouts, but let Sony's widget supply its
     * own edges/focus instead of drawing the Legacy border over it. */
    if (ui_native_vita_panel(x, y, width, height, fill, focused)) {
        if (focused) {
            UiFocusMotion focus={.x=x,.y=y,.width=width,.height=height,.initialized=1};
            ui_focus_outline_draw(&focus);
        }
        return;
    }
#endif
    ui_theme_fx_surface(x,y,width,height,fill,focused);
	vita2d_draw_rectangle(x, y, width, 1,
	                      focused ? VT_THEME_FOCUS : VT_THEME_BORDER_DIM);
	vita2d_draw_rectangle(x, y + height - 1, width, 1,
		                      VT_THEME_BORDER_DIM);
	/* Focus is structural, not four brackets competing with nearby text. */
	if (focused) {
		UiFocusMotion focus={.x=x,.y=y,.width=width,.height=height,.initialized=1};
		ui_focus_outline_draw(&focus);
	}
	(void)accent;
}

/* Interpolated GXM material: one batch, no per-pixel CPU drawing. */
static void menu_gradient(float x,float y,float w,float h,unsigned top,unsigned bottom){
 vita2d_color_vertex v[6]={{x,y,.5f,top},{x+w,y,.5f,top},{x,y+h,.5f,bottom},
                         {x+w,y,.5f,top},{x+w,y+h,.5f,bottom},{x,y+h,.5f,bottom}};
#ifdef __vita__
 vita2d_color_vertex *gpu=vita2d_pool_memalign(sizeof(v),16);
 if(!gpu)return;
 memcpy(gpu,v,sizeof(v));
#else
 vita2d_color_vertex *gpu=v;
#endif
#ifdef VMD_ENABLE_PAF_NATIVE
 ui_native_vita_cover(x,y,w,h,top);
#endif
 vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,gpu,6);
}
void ui_menu_surface(float x,float y,float width,float height,float opacity){
 if(x==0&&width>=960&&height>=400)return;
 unsigned a=(unsigned)(255*fminf(1,fmaxf(0,opacity)));
 if(ui_theme_id()!=VT_UI_THEME_LEGACY_VITA){
  /* Full-page menus keep the theme's own scene visible. Drawers and dialogs
   * use its surface instead of laying a blue sheet over every theme. */
  if(x==0&&width>=960&&height>=400)return;
  vita2d_draw_rectangle(x,y,width,height,ui_theme_alpha(UI_MENU_BG,a));
  ui_theme_fx_surface(x,y,width,height,UI_MENU_BG,0);
  return;
 }
 /* The blue analogue of the Vita Settings luminous, vertically shaded canvas. */
 menu_gradient(x,y,width,height,RGBA8(9,45,85,a),RGBA8(3,19,45,a));
 menu_gradient(x,y+height*.44f,width,height*.56f,RGBA8(17,85,137,a/5),RGBA8(7,47,85,a/2));
}
void ui_menu_row(float x,float y,float width,float height,int focused,float opacity){
 (void)focused;
 unsigned a=(unsigned)(180*fminf(1,fmaxf(0,opacity)));
#ifdef VMD_ENABLE_PAF_NATIVE
 int prior=ui_native_vita_decoration(1);
#endif
 unsigned line=ui_theme_id()==VT_UI_THEME_LEGACY_VITA?UI_MENU_ACCENT:VT_THEME_BORDER;
 for(int i=4;i>=1;i--)
  vita2d_draw_rectangle(x,y+height-1-i,width,1,ui_theme_alpha(line,a/(12*i)));
 vita2d_draw_rectangle(x,y+height-1,width,1,ui_theme_alpha(line,a));
#ifdef VMD_ENABLE_PAF_NATIVE
 ui_native_vita_decoration(prior);
#endif
}
void ui_menu_focus_draw(const UiFocusMotion *f){
 if(!f||!f->initialized||f->width<=0||f->height<=0)return;
 if(ui_theme_id()!=VT_UI_THEME_LEGACY_VITA){ui_focus_outline_draw(f);return;}
#ifdef VMD_ENABLE_PAF_NATIVE
 if(ui_native_vita_focus(f->x,f->y-6,f->width,f->height+12))return;
 int prior=ui_native_vita_decoration(1);
#endif
 /* A single gradient selection band moves through the gaps. The PAF glow
  * plane above it uses the firmware common texture; no empty Button widgets. */
 menu_gradient(f->x,f->y,f->width,f->height*.52f,RGBA8(98,197,246,78),RGBA8(40,133,209,42));
 menu_gradient(f->x,f->y+f->height*.52f,f->width,f->height*.48f,RGBA8(23,105,185,36),RGBA8(122,220,255,76));
 for(int i=7;i>=1;i--){
  unsigned a=45/(i+1);
  vita2d_draw_rectangle(f->x,f->y-i,f->width,1,RGBA8(100,205,255,a));
  vita2d_draw_rectangle(f->x,f->y+f->height+i-1,f->width,1,RGBA8(100,205,255,a));
 }
 vita2d_draw_rectangle(f->x,f->y,f->width,1,RGBA8(181,236,255,180));
 vita2d_draw_rectangle(f->x,f->y+f->height-1,f->width,1,RGBA8(181,236,255,180));
#ifdef VMD_ENABLE_PAF_NATIVE
 ui_native_vita_focus(f->x,f->y-6,f->width,f->height+12);
 ui_native_vita_decoration(prior);
#endif
}
void ui_menu_text(float x,float baseline,float width,const char *text,unsigned color,unsigned size){
 vita2d_font *font=ui_runtime_font(size);if(!font||!text||width<=0)return;
 char fitted[512];ui_font_fit_text(font,size,text,fitted,sizeof(fitted),(int)width);
 /* Native captions have explicit component bounds, never the remaining
  * screen width: an adjacent icon/value must not hide the whole label. */
#ifdef VMD_ENABLE_PAF_NATIVE
 if(ui_native_vita_text(x,baseline,width,size,color,fitted))return;
#endif
 ui_font_draw_text_exact(font,x,baseline,color,size,fitted);
}

void ui_action_button(float x, float y, float width, float height,
	                  unsigned int fill, const char *key, const char *label,
	                  int active) {
	/* Face-button names stay consistent with the pad and translated help. */
 if(key){
  if(!strcmp(key,"Cross") || !strcmp(key,"X"))key=UI_PAD_CROSS;
  else if(!strcmp(key,"Circle") || !strcmp(key,"O"))key=UI_PAD_CIRCLE;
  else if(!strcmp(key,"Triangle"))key=UI_PAD_TRIANGLE;
  else if(!strcmp(key,"Square"))key=UI_PAD_SQUARE;
  else if(!strcmp(key,"L1"))key=UI_PAD_L1;
  else if(!strcmp(key,"R1"))key=UI_PAD_R1;
 }
 unsigned int foreground=(ui_contrast_bw(fill)&0xffffffU)|(fill&0xff000000U);
 if(label&&label[0])ui_menu_row(x,y,width,height,active,(fill>>24)/255.f);
 vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
 float key_width = 0.0f;
 if (small && key && key[0]) {
	 key_width = (float)ui_font_text_width(small, UI_FONT_SMALL, key) + 14.0f;
	 if (key_width < 34.0f)
		 key_width = 34.0f;
 }
 if (key_width > 0.0f && label && label[0]) {
	 if (small) {
		 int kw = ui_font_text_width(small, UI_FONT_SMALL, key);
		 ui_font_draw_text_exact(small, (int)(x + 10 + (key_width - kw) * .5f),
			               (int)(y + height * .5f + 7), foreground, UI_FONT_SMALL, key);
	 }
 }
 if (small && key && key[0] && (!label || !label[0])) {
	 char fitted[128];
	 int kw = ui_font_fit_text(small, UI_FONT_SMALL, key, fitted, sizeof(fitted), (int)width - 8);
	 ui_font_draw_text_exact(small, (int)(x + (width - kw) * .5f),
	                   (int)(y + height * .5f + 7), foreground, UI_FONT_SMALL, fitted);
 }
 if (small && label) {
	 char fitted[192];
	 float left = x + (key_width>0?key_width+22.0f:14.0f);
	 float available = x+width-14-left;
	 ui_font_fit_text(small, UI_FONT_SMALL, label, fitted, sizeof(fitted), (int)available);
	 ui_menu_text(left,y+height*.5f+7,available,fitted,foreground,UI_FONT_SMALL);
 }
}

void ui_watched_progress(const char *history_id, float x, float y, float width) {
	uint64_t position_ms = 0, duration_ms = 0;
	if (!history_id || width < 24.0f ||
	    !vt_playback_history_progress(history_id, &position_ms, &duration_ms) ||
	    !duration_ms) return;
	if (position_ms > duration_ms) position_ms = duration_ms;
	float fraction = (float)((double)position_ms / (double)duration_ms);
	unsigned int percent = (unsigned int)((position_ms * 100ULL +
	                                      duration_ms / 2ULL) / duration_ms);
	if (percent > 100U) percent = 100U;
	vita2d_draw_rectangle(x, y, width, 6, RGBA8(0, 0, 0, 224));
	vita2d_draw_rectangle(x + 1, y + 1, (width - 2) * fraction, 4,
	                      percent >= 100U ? VT_THEME_SIGNAL_LIGHT
	                                      : VT_THEME_SPECTRAL);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (small) {
		char label[8];
		snprintf(label, sizeof(label), "%u%%", percent);
		int label_width = ui_font_text_width(small, UI_FONT_SMALL, label);
		float pill_width = label_width + 10.0f;
		vita2d_draw_rectangle(x + width - pill_width, y - 19, pill_width, 18,
		                      RGBA8(0, 0, 0, 210));
		ui_font_draw_text(small, (int)(x + width - label_width - 5), (int)y - 5,
		                  RGBA8(234,245,240,255), UI_FONT_SMALL, label);
	}
}
