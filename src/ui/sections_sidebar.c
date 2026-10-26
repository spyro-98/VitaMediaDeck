#include "ui/sections_sidebar.h"
#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "network/download_manager.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/home_tabs.h"
#include "ui/online_audio_screen.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "ui/theme_home.h"
#include "ui/theme_scene.h"
#include "ui/page_motion.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui/components.h"
#include "ui/touch.h"

#define SCREEN_HEIGHT 544
#define SIDEBAR_WIDTH 424
#define ITEM_X 20
#define ITEM_W 384
#define SIDEBAR_CONTENT_COUNT (UI_SECTION_COUNT - 3)

#define ANALOG_LOW 64
#define ANALOG_HIGH 191
#define NAV_REPEAT_DELAY_US 280000ULL
#define NAV_REPEAT_INTERVAL_US 95000ULL

static int g_requested_section=UI_SECTION_NONE;
void ui_sections_request(int section) {
 if(section>=0 && section<UI_SECTION_ACTION_COUNT)g_requested_section=section;
}
int ui_sections_take_requested(void) {
 int result=g_requested_section;g_requested_section=UI_SECTION_NONE;return result;
}
void ui_nav_repeat_reset(UiNavRepeat *repeat) {
	if (!repeat) return;
	repeat->direction = 0;
	repeat->repeat_next_us = 0;
}

static unsigned int held_direction(unsigned int buttons,
	                               unsigned char analog_x,
	                               unsigned char analog_y,
	                               unsigned int allowed) {
	/* Digital input wins. It is exact and should never be displaced by a stick
	 * that is merely resting a few units away from its centre. */
	if ((allowed & SCE_CTRL_UP) && (buttons & SCE_CTRL_UP)) return SCE_CTRL_UP;
	if ((allowed & SCE_CTRL_DOWN) && (buttons & SCE_CTRL_DOWN)) return SCE_CTRL_DOWN;
	if ((allowed & SCE_CTRL_LEFT) && (buttons & SCE_CTRL_LEFT)) return SCE_CTRL_LEFT;
	if ((allowed & SCE_CTRL_RIGHT) && (buttons & SCE_CTRL_RIGHT)) return SCE_CTRL_RIGHT;

	int dx = (int)analog_x - 128;
	int dy = (int)analog_y - 128;
	int abs_x = dx < 0 ? -dx : dx;
	int abs_y = dy < 0 ? -dy : dy;
	int horizontal = ((allowed & SCE_CTRL_LEFT) && analog_x < ANALOG_LOW) ||
	                 ((allowed & SCE_CTRL_RIGHT) && analog_x > ANALOG_HIGH);
	int vertical = ((allowed & SCE_CTRL_UP) && analog_y < ANALOG_LOW) ||
	               ((allowed & SCE_CTRL_DOWN) && analog_y > ANALOG_HIGH);
	/* A diagonal stick position resolves to the dominant axis. This makes grid
	 * movement predictable instead of producing two focus changes in one frame. */
	if (horizontal && (!vertical || abs_x > abs_y))
		return dx < 0 ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT;
	if (vertical)
		return dy < 0 ? SCE_CTRL_UP : SCE_CTRL_DOWN;
	return 0;
}

unsigned int ui_nav_repeat_update(UiNavRepeat *repeat,
	                              unsigned int pressed,
	                              unsigned int held_buttons,
	                              unsigned char analog_x,
	                              unsigned char analog_y,
	                              unsigned int allowed_directions) {
	if (!repeat) return pressed & allowed_directions;
	unsigned int direction = held_direction(held_buttons, analog_x, analog_y,
	                                        allowed_directions);
	unsigned int navigation = pressed & allowed_directions;
	uint64_t now = sceKernelGetProcessTimeWide();
	if (!direction) {
		repeat->direction = 0;
	} else if (direction != repeat->direction) {
		repeat->direction = direction;
		repeat->repeat_next_us = now + NAV_REPEAT_DELAY_US;
		navigation |= direction;
	} else if (now >= repeat->repeat_next_us) {
		repeat->repeat_next_us = now + NAV_REPEAT_INTERVAL_US;
		navigation |= direction;
	}
	return navigation;
}

/* Presentation order is independent of the stable section/action identifiers. */
static int sidebar_section_at(const UiSectionsSidebar *sidebar, int slot) {
    static const int legacy_order[] = {
        UI_SECTION_LOCAL_MEDIA, UI_SECTION_NETWORK, UI_SECTION_JELLYFIN, UI_SECTION_NAVIDROME,
        UI_SECTION_IMMICH, UI_SECTION_ONLINE_AUDIO, UI_SECTION_IPTV,
        UI_SECTION_SETTINGS, UI_SECTION_GUIDE, UI_SECTION_INFO
    };
    if (sidebar->player_available && slot == 0) return UI_SECTION_PLAYER;
    if (sidebar->download_available && slot == sidebar->player_available)
        return UI_SECTION_DOWNLOAD;
    _Static_assert(sizeof(legacy_order)/sizeof(legacy_order[0])==UI_SECTION_COUNT,"Section order must cover every provider");
    int index = slot - sidebar->player_available - sidebar->download_available;
    if (index < 0 || index >= UI_SECTION_COUNT) return UI_SECTION_NONE;
    return legacy_order[index];
}
static int sidebar_active_slot(const UiSectionsSidebar *sidebar) {
    int count = UI_SECTION_COUNT + sidebar->player_available + sidebar->download_available;
    for (int i = 0; i < count; i++)
        if (sidebar_section_at(sidebar, i) == sidebar->active) return i;
    return sidebar->player_available + sidebar->download_available;
}
static int sidebar_view_top(void) {
	return vt_ui_theme_legacy_layout(ui_theme_id()) ? 104 : 148;
}
static int sidebar_view_bottom(void) { return 478; }
static float sidebar_row_y(const UiSectionsSidebar *sidebar,float slot) {
    int live=sidebar->player_available+sidebar->download_available;
    int count=UI_SECTION_COUNT+live;
    if(slot<0)slot=0;
    if(slot>count-1)slot=count-1;
    int index=(int)slot;
    float y=144+index*56;
    if(live&&index>=live)y+=38;
    if(index>=live+SIDEBAR_CONTENT_COUNT)y+=38;
    if(index+1<count) {
        int gap=(live&&index+1==live)||index+1==live+SIDEBAR_CONTENT_COUNT;
        y+=(slot-index)*(56+(gap?38:0));
    }
    return y;
}
static int sidebar_row_height(void) { return 42; }
static int sidebar_vertical(void) {
	int theme = ui_theme_id();
	return vt_ui_theme_legacy_layout(theme) || theme == VT_UI_THEME_DECK_90 ||
	       theme == VT_UI_THEME_APERTURE;
}
static UiThemeRect sidebar_content_rect(const UiSectionsSidebar *sidebar,int slot) {
    int theme=ui_theme_id(),live=sidebar->player_available+sidebar->download_available;
	if (vt_ui_theme_legacy_layout(theme)) {
		return (UiThemeRect){ITEM_X, (int)sidebar_row_y(sidebar, (float)slot), ITEM_W,
		                     sidebar_row_height()};
	}
	if (slot < live)
		return (UiThemeRect){64 + slot * 428, 166, live == 1 ? 832 : 404, 42};
	int index = slot - live, base = live ? 224 : 188;
	if (theme == VT_UI_THEME_DECK_90)
		return (UiThemeRect){104, base + index * 64 + (index >= SIDEBAR_CONTENT_COUNT ? 36 : 0),
		                     752, 48};
	if (theme == VT_UI_THEME_APERTURE)
		return (UiThemeRect){104, base + index * 72 + (index >= SIDEBAR_CONTENT_COUNT ? 36 : 0),
		                     752, 52};
	if (theme == VT_UI_THEME_GHOST_MESH) {
		if (index < SIDEBAR_CONTENT_COUNT) {
			float angle=-1.57079632679f+index*(6.28318530718f/SIDEBAR_CONTENT_COUNT);
			return (UiThemeRect){(int)lroundf(350 + cosf(angle) * 102) - 37,
			                     (int)lroundf(334 + sinf(angle) * 102) - 21, 74, 42};
		}
		return (UiThemeRect){704, 238 + (index - SIDEBAR_CONTENT_COUNT) * 78, 192, 54};
	}
	if (theme == VT_UI_THEME_SIGNAL) {
		if (index < SIDEBAR_CONTENT_COUNT)
			return (UiThemeRect){64 + (index % 2) * 286, (live ? 240 : 188) + (index / 2) * 58, 268, 44};
		return (UiThemeRect){664, (live ? 240 : 188) + (index - SIDEBAR_CONTENT_COUNT) * 80, 232, 58};
	}
	int app = index >= SIDEBAR_CONTENT_COUNT;
	int local = app ? index - SIDEBAR_CONTENT_COUNT : index, row = local / 3, col = local % 3;
	if (theme == VT_UI_THEME_RELAY && !app && row % 2)
		col = 2 - col;
	return (UiThemeRect){64 + col * 282, app ? 416 : base + (live ? 16 : 0) + row * (live ? 50 : 58), 264,
                         app || live ? 44 : 48};
}

static UiThemeRect sidebar_slot_rect(const UiSectionsSidebar *sidebar,int slot) {
    UiThemeRect r=sidebar_content_rect(sidebar,slot);
    r.y-=(int)lroundf(sidebar->scroll);
    return r;
}
static float sidebar_scroll_max(const UiSectionsSidebar *sidebar) {
	if (!sidebar_vertical())
		return 0;
	int count = UI_SECTION_COUNT + sidebar->player_available + sidebar->download_available;
	float bottom = 0;
	for(int i=0;i<count;i++) {
        UiThemeRect r=sidebar_content_rect(sidebar,i);
        if(r.y+r.h>bottom)bottom=(float)(r.y+r.h);
    }
    float maximum=bottom+18-sidebar_view_bottom();
    return maximum>0?maximum:0;
}
static float sidebar_scroll_clamp(const UiSectionsSidebar *sidebar,float value) {
    float maximum=sidebar_scroll_max(sidebar);
    if(!isfinite(value)||value<0)return 0;
    return value>maximum?maximum:value;
}
static void sidebar_scroll_to_cursor(UiSectionsSidebar *sidebar) {
	if (!sidebar_vertical()) {
		sidebar->scroll = sidebar->scroll_target = sidebar->scroll_velocity = 0;
		return;
	}
	UiThemeRect r = sidebar_content_rect(sidebar, sidebar->cursor);
	float target = sidebar->scroll_target;
	int live=sidebar->player_available+sidebar->download_available;
    int margin=sidebar->cursor==0||sidebar->cursor==live||sidebar->cursor==live+SIDEBAR_CONTENT_COUNT?36:18;
    if(r.y-target<sidebar_view_top()+margin)target=r.y-sidebar_view_top()-margin;
    if(r.y+r.h-target>sidebar_view_bottom()-18)target=r.y+r.h-sidebar_view_bottom()+18;
    sidebar->scroll_target=sidebar_scroll_clamp(sidebar,target);
}
static int sidebar_rect_visible(UiThemeRect r) {
    return r.y+r.h>sidebar_view_top() && r.y<sidebar_view_bottom();
}
static void sidebar_focus_visible(UiSectionsSidebar *sidebar) {
    int count=UI_SECTION_COUNT+sidebar->player_available+sidebar->download_available;
    float center=(sidebar_view_top()+sidebar_view_bottom())*.5f,best=1e9f;
    int next=sidebar->cursor;
    for(int i=0;i<count;i++) {
        UiThemeRect r=sidebar_content_rect(sidebar,i);
        float y=r.y-sidebar->scroll_target;
        if(y<sidebar_view_top()+8 || y+r.h>sidebar_view_bottom()-8)continue;
        float score=fabsf(y+r.h*.5f-center);
        if(score<best){best=score;next=i;}
    }
    sidebar->cursor=next;
}
static int sidebar_neighbour(const UiSectionsSidebar *s, unsigned nav) {
	UiThemeRect from = sidebar_content_rect(s, s->cursor);
	float x = from.x + from.w * .5f, y = from.y + from.h * .5f, best = 1e9f;
	int next = s->cursor, count = UI_SECTION_COUNT + s->player_available + s->download_available;
	int vertical = !!(nav & (SCE_CTRL_UP | SCE_CTRL_DOWN));
	int sign = (nav & (SCE_CTRL_UP | SCE_CTRL_LEFT)) ? -1 : 1;
	for (int i = 0; i < count; i++)
		if (i != s->cursor) {
			UiThemeRect r = sidebar_content_rect(s, i);
			float dx = r.x + r.w * .5f - x, dy = r.y + r.h * .5f - y;
			float along = vertical ? dy : dx, across = vertical ? dx : dy;
			if (along * sign <= 8)
				continue;
			float score = fabsf(along) + fabsf(across) * 2.4f;
			if (score < best) {
				best = score;
				next = i;
			}
		}
	return next;
}
static float sidebar_offset(const UiSectionsSidebar *sidebar) {
 float travel=1-sidebar->animation;
 int theme=ui_theme_id();UiThemeMotionProfile profile=ui_theme_motion_profile(theme);
 if(theme==VT_UI_THEME_GHOST_MESH||theme==VT_UI_THEME_SIGNAL||theme==VT_UI_THEME_RELAY)travel*=travel;
 else if(theme==VT_UI_THEME_APERTURE||theme==VT_UI_THEME_MOSAIC)travel*=.6f+.4f*travel;
 return (profile.menu_from_right ? 1 : -1) *
	    (vt_ui_theme_legacy_layout(theme) ? SIDEBAR_WIDTH : 960) * travel;
}
static int sidebar_clip_body(float ox) {
	int legacy = vt_ui_theme_legacy_layout(ui_theme_id());
	int left = (int)ox + (legacy ? 0 : 48), right = (int)ox + (legacy ? SIDEBAR_WIDTH : 912);
	if (left < 0)
		left = 0;
	if (right > 960)
		right = 960;
	if (right <= left)
		return 0;
	vita2d_set_clip_rectangle(left, sidebar_view_top(), right, sidebar_view_bottom());
	vita2d_enable_clipping();return 1;
}
static void sidebar_group_headings(const UiSectionsSidebar *sidebar,float ox) {
	int live = sidebar->player_available + sidebar->download_available,
	    legacy = vt_ui_theme_legacy_layout(ui_theme_id());
	const int starts[] = {0, live, live + SIDEBAR_CONTENT_COUNT};
	const VtStringId labels[] = {VT_STR_SECTIONS_GROUP_LIVE, VT_STR_SECTIONS_GROUP_CONTENT,
	                             VT_STR_SECTIONS_GROUP_APP};
	for (int group = live ? 0 : 1; group < 3; group++) {
		UiThemeRect r = sidebar_slot_rect(sidebar, starts[group]);
		int x = legacy ? 34 : r.x + 18;
		if(ui_theme_id()==VT_UI_THEME_GHOST_MESH)x=group==2?716:64;
        if(ui_theme_id()==VT_UI_THEME_SIGNAL)x=group==2?664:64;
        int y=r.y-14;
        if (!legacy && sidebar_vertical() && live && group == 1) y = r.y - 8;
		if (!sidebar_vertical()) {
			if (group == 0)
				continue;
			y = group == 2 ? 408 : (live ? 228 : 178);
			x = group == 2 ? 64 : 64;
			if (ui_theme_id() == VT_UI_THEME_GHOST_MESH) {
				x = group == 2 ? 704 : 64;
				y = live ? 228 : (group == 2 ? 224 : 210);
			}
			if (ui_theme_id() == VT_UI_THEME_SIGNAL && group == 2) {
				x = 664;
				y = live ? 228 : 178;
			}
		}
		/* Ghost's first node is at the top of its orbit, not at the left edge. */
		if (y - UI_FONT_SMALL < sidebar_view_top() || y + 4 > sidebar_view_bottom())
			continue;
		const char *label=vt_i18n_str(labels[group]);
        ui_text_line(ox+x,y,legacy?350:group==2&&ui_theme_id()==VT_UI_THEME_GHOST_MESH?184:832-x+64,
                     label,legacy?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
	}
}
static void sidebar_scroll_chrome(const UiSectionsSidebar *sidebar,float ox) {
	int legacy = vt_ui_theme_legacy_layout(ui_theme_id());
	float maximum = sidebar_scroll_max(sidebar);
	int left = legacy ? 28 : 64, width = legacy ? 272 : 704;
#ifdef VMD_ENABLE_PAF_NATIVE
    if (ui_native_vita_active())
        ui_panel(ox + 14, 478, SIDEBAR_WIDTH - 28, 62, VT_THEME_SURFACE, VT_THEME_BORDER_DIM, 0);
#endif
	ui_text_line(
	    ox + left, 503, width,
	    vt_i18n_str(sidebar_vertical() ? VT_STR_SECTIONS_SCROLL_HINT : VT_STR_SECTIONS_SPATIAL_HINT),
	    VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
	ui_text_line(ox + left, 528, width,
	             vt_i18n_str(sidebar->audio_layout_available ? VT_STR_MUSIC_LAYOUT_HINT
	                                                         : VT_STR_SECTIONS_FOOTER_HINT),
	             VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
	if (maximum <= 0)
		return;
	float top=sidebar_view_top()+6,height=sidebar_view_bottom()-sidebar_view_top()-12;
    float thumb=height*height/(height+maximum);
    if(thumb<30)thumb=30;
    float x=ox+(legacy?412:908),y=top+(height-thumb)*(sidebar->scroll/maximum);
    vita2d_draw_rectangle(x,top,3,height,VT_THEME_BORDER_DIM);
    vita2d_draw_rectangle(x,y,3,thumb,VT_THEME_SIGNAL_BRIGHT);
    for(int down=0;down<2;down++) {
        int bx=(legacy?318:806)+down*(legacy?48:50),by=494;
        int enabled=down?sidebar->scroll_target<maximum-.5f:sidebar->scroll_target>.5f;
        ui_panel(ox+bx,by,36,34,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
        float cx=ox+bx+18,cy=by+17,sign=down?1:-1;
        unsigned ink=enabled?VT_THEME_TEXT:VT_THEME_TEXT_FAINT;
        vita2d_draw_line(cx-6,cy-sign*3,cx,cy+sign*3,ink);
        vita2d_draw_line(cx,cy+sign*3,cx+6,cy-sign*3,ink);
    }
}
static UiThemeRect sidebar_layout_rect(int index){return (UiThemeRect){64+index*284,228,264,188};}
static void sidebar_themed_draw(const UiSectionsSidebar *sidebar,const char *const items[]) {
 float ox=sidebar_offset(sidebar);int theme=ui_theme_id();
 ui_theme_scene_begin(UI_SCENE_MENU,"sections");
 vita2d_set_clip_rectangle(0,54,960,544);vita2d_enable_clipping();
 vita2d_draw_rectangle(0,54,960,490,RGBA8(0,0,0,(unsigned)(190*sidebar->animation)));
 unsigned fill=theme==VT_UI_THEME_SIGNAL?VT_THEME_BG:VT_THEME_BG_SOFT;
 vita2d_draw_rectangle(ox+36,70,888,464,fill);
 ui_theme_fx_surface(ox+36,70,888,464,fill,0);
 /* The command deck adopts each shell's visual grammar and leaves the real
  * system status visible above it. It is never the Legacy side drawer. */
 if(theme==VT_UI_THEME_DECK_90){
  for(int y=76;y<528;y+=8)vita2d_draw_rectangle(ox+40,y,880,1,RGBA8(0,0,0,20));
  vita2d_draw_rectangle(ox+36,70,888,2,VT_THEME_SIGNAL_BRIGHT);
 }else if(theme==VT_UI_THEME_SIGNAL){
  for(int i=0;i<22;i++)vita2d_draw_rectangle(ox+862+i,82+i,26,1,VT_THEME_SIGNAL_BRIGHT);
 }else if(theme==VT_UI_THEME_GHOST_MESH){
  vita2d_draw_line(ox+36,88,ox+54,70,VT_THEME_COLD);vita2d_draw_line(ox+906,534,ox+924,516,VT_THEME_COLD);
 }else if(theme==VT_UI_THEME_APERTURE){
  vita2d_draw_rectangle(ox+36,70,888,4,VT_THEME_COLD);vita2d_draw_rectangle(ox+36,530,888,4,VT_THEME_COLD);
 }else if(theme==VT_UI_THEME_RELAY){
  vita2d_draw_rectangle(ox+620,180,1,300,VT_THEME_BORDER);
 }else if(theme==VT_UI_THEME_MOSAIC){
  vita2d_draw_rectangle(ox+48,82,864,54,VT_THEME_SURFACE_RAISED);
 }
 float model_x=theme==VT_UI_THEME_APERTURE?480:theme==VT_UI_THEME_GHOST_MESH?350:820;
 float model_y=theme==VT_UI_THEME_APERTURE?350:theme==VT_UI_THEME_GHOST_MESH?342:172;
 if(theme!=VT_UI_THEME_GHOST_MESH)ui_theme_scene_model(UI_SCENE_MENU,ox+model_x,model_y,theme==VT_UI_THEME_APERTURE?210:theme==VT_UI_THEME_GHOST_MESH?120:theme==VT_UI_THEME_DECK_90?130:78,.55f,1);
 ui_text_line(ox+64,118,230,vt_i18n_str(VT_STR_SECTIONS_TITLE),sidebar->audio_layout_page?VT_THEME_TEXT_MUTED:VT_THEME_TEXT,UI_FONT_DISPLAY);
 ui_text_line(ox+850,115,48,"L1",VT_THEME_SIGNAL_BRIGHT,UI_FONT_BODY);
 if(sidebar->audio_layout_available){
  ui_text_line(ox+320,118,480,vt_i18n_str(VT_STR_MUSIC_LAYOUT_PLAYER),sidebar->audio_layout_page?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT_MUTED,UI_FONT_BODY);
  vita2d_draw_rectangle(ox+(sidebar->audio_layout_page?320:64),130,148,2,VT_THEME_SIGNAL_BRIGHT);
 }
 if(sidebar->audio_layout_page&&sidebar->audio_layout_available){
  ui_text_line(ox+64,188,832,vt_i18n_str(VT_STR_MUSIC_LAYOUT_TITLE),VT_THEME_TEXT,UI_FONT_BODY);
  const VtStringId choices[]={VT_STR_MUSIC_LAYOUT_THEME,VT_STR_MUSIC_LAYOUT_LEFT,VT_STR_MUSIC_LAYOUT_RIGHT};
  for(int i=0;i<3;i++){
   UiThemeRect r=sidebar_layout_rect(i);r.x+=(int)ox;
   ui_theme_fx_surface(r.x,r.y,r.w,r.h,i==sidebar->audio_layout_cursor?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE,i==sidebar->audio_layout_cursor);

   /* Diagram represents cover placement, never simulated playback data. */
   vita2d_draw_rectangle(r.x+18,r.y+20,r.w-36,94,VT_THEME_BG);
   UiAudioLayout preview=ui_theme_audio_layout(i,1);
   int picture_x=r.x+18+(int)((preview.cover_x+preview.cover_size*.5f)/960*(r.w-36))-29;
   vita2d_draw_rectangle(picture_x,r.y+38,58,58,VT_THEME_COLD);
   ui_text_line(r.x+18,r.y+148,r.w-36,vt_i18n_str(choices[i]),VT_THEME_TEXT,UI_FONT_BODY);
   if(i==sidebar->audio_layout_selected)vita2d_draw_rectangle(r.x+18,r.y+r.h-14,r.w-36,2,VT_THEME_SIGNAL_BRIGHT);
   if(sidebar->open&&i==sidebar->audio_layout_cursor){UiFocusMotion focus={.x=r.x,.y=r.y,.width=r.w,.height=r.h,.initialized=1};ui_focus_outline_draw(&focus);}
  }
  ui_text_line(ox+64,470,832,vt_i18n_str(VT_STR_MUSIC_LAYOUT_APPLIES),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }else if(sidebar_clip_body(ox)){
  if(theme==VT_UI_THEME_GHOST_MESH) {
   int live=sidebar->player_available+sidebar->download_available;
   (void)live;
   ui_theme_scene_model(UI_SCENE_MENU, ox + 350, 334, 96, .55f, 1);
  }
  sidebar_group_headings(sidebar,ox);
  int count=UI_SECTION_COUNT+sidebar->player_available+sidebar->download_available;
  for(int i=0;i<count;i++){
   UiThemeRect r=sidebar_slot_rect(sidebar,i);
   if(!sidebar_rect_visible(r))continue;
   r.x+=(int)ox;int section=sidebar_section_at(sidebar,i);
   const char *label=section==UI_SECTION_PLAYER?vt_i18n_str(VT_STR_SECTIONS_PLAYER):section==UI_SECTION_DOWNLOAD?vt_i18n_str(VT_STR_SECTIONS_DOWNLOAD):items[section];
   int selected=sidebar->open&&i==sidebar->cursor;
   unsigned background=selected?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE;
   unsigned fg=selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT;
   ui_theme_scene_card(UI_SCENE_MENU,r,selected,i);
   if(theme!=VT_UI_THEME_APERTURE)ui_theme_fx_surface(r.x+3,r.y+2,r.w-6,r.h-4,background,selected);
   else vita2d_draw_rectangle(r.x+5,r.y+4,r.w-10,r.h-8,background);
   int ghost = theme == VT_UI_THEME_GHOST_MESH &&
			   i >= sidebar->player_available + sidebar->download_available &&
			   i < sidebar->player_available + sidebar->download_available + SIDEBAR_CONTENT_COUNT;
   ui_text_line(r.x + (ghost ? 6 : 18), r.y + r.h / 2 + 6, r.w - (ghost ? 12 : 36), label, fg,
			    ghost ? UI_FONT_SMALL : UI_FONT_BODY);
   if(section==sidebar->active)vita2d_draw_rectangle(r.x+8,r.y+8,3,r.h-16,VT_THEME_COLD);
  }
  if (sidebar->open && theme == VT_UI_THEME_GHOST_MESH) {
	  int selected = sidebar_section_at(sidebar, sidebar->cursor);
	  const char *title = selected == UI_SECTION_PLAYER     ? vt_i18n_str(VT_STR_SECTIONS_PLAYER)
			              : selected == UI_SECTION_DOWNLOAD ? vt_i18n_str(VT_STR_SECTIONS_DOWNLOAD)
			                                                : items[selected];
	  ui_text_line(ox + 252, 338, 196, title, VT_THEME_TEXT, UI_FONT_SMALL);
  }
  if (sidebar->open && !sidebar_vertical()) {
	  UiFocusMotion focus = sidebar->fixed_focus;
	  focus.x += ox;
	  ui_focus_outline_draw(&focus);
  }
  if (sidebar->open && sidebar_vertical()) {
	  float cursor = sidebar->focus_cursor;
	  if (cursor < 0)
		  cursor = 0;
	  if (cursor > count - 1)
		  cursor = count - 1;
	  int a = (int)cursor, b = a + 1 < count ? a + 1 : a;
	  float t = cursor - a;
	  UiThemeRect from = sidebar_slot_rect(sidebar, a), to = sidebar_slot_rect(sidebar, b);
	  UiFocusMotion focus = {.x = ox + from.x + (to.x - from.x) * t,
			                 .y = from.y + (to.y - from.y) * t,
			                 .width = from.w + (to.w - from.w) * t,
			                 .height = from.h + (to.h - from.h) * t,
			                 .initialized = 1};
	  ui_focus_outline_draw(&focus);
  }
 }
 vita2d_set_clip_rectangle(0,54,960,544);
 if(!sidebar->audio_layout_page)sidebar_scroll_chrome(sidebar,ox);
 else ui_text_line(ox+64,516,832,vt_i18n_str(VT_STR_MUSIC_LAYOUT_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 vita2d_disable_clipping();
}

void ui_sections_sidebar_init(UiSectionsSidebar *sidebar, int active) {
	if (!sidebar) return;
	sidebar->open = 0;
	sidebar->active = active >= 0 && active < UI_SECTION_COUNT
	                ? active : UI_SECTION_LOCAL_MEDIA;
	sidebar->player_available = vt_background_playback_can_resume_fullscreen();
	sidebar->download_available = vt_download_background_active();
	sidebar->cursor = sidebar_active_slot(sidebar);
	sidebar->animation = 0.0f;
	sidebar->focus_cursor = (float)sidebar->cursor;
	sidebar->repeat_button = 0;
	sidebar->repeat_next_us = 0;
	sidebar->animation_last_us = 0;
	sidebar->audio_layout_available=sidebar->audio_layout_page=0;
	sidebar->audio_layout_cursor=sidebar->audio_layout_selected=0;
	sidebar->scroll=sidebar->scroll_target=sidebar->scroll_velocity=0;
	sidebar->scroll_theme=ui_theme_id();
	ui_focus_motion_reset(&sidebar->fixed_focus);
	sidebar->touch_active=sidebar->touch_dragged=sidebar->touch_start_y=0;
	sidebar->touch_start_scroll=0;
	sidebar_scroll_to_cursor(sidebar);
	sidebar->scroll=sidebar->scroll_target;
}

static void sync_live_slots(UiSectionsSidebar *sidebar) {
	if (!sidebar) return;
	int player_available = vt_background_playback_can_resume_fullscreen();
	int download_available = vt_download_background_active();
	if (player_available == sidebar->player_available &&
	    download_available == sidebar->download_available) return;
	int focused=sidebar_section_at(sidebar,sidebar->cursor);
	UiThemeRect before=sidebar_content_rect(sidebar,sidebar->cursor);
	sidebar->player_available=player_available;
	sidebar->download_available=download_available;
	int count=UI_SECTION_COUNT+player_available+download_available,next=-1;
	for(int i=0;i<count;i++)if(sidebar_section_at(sidebar,i)==focused){next=i;break;}
	sidebar->cursor=next>=0?next:sidebar_active_slot(sidebar);
	UiThemeRect after=sidebar_content_rect(sidebar,sidebar->cursor);
	sidebar->scroll=sidebar_scroll_clamp(sidebar,sidebar->scroll+after.y-before.y);
	sidebar->scroll_target=sidebar_scroll_clamp(sidebar,sidebar->scroll_target+after.y-before.y);
	sidebar->scroll_velocity=0;
	if(!sidebar->audio_layout_page)sidebar->focus_cursor=(float)sidebar->cursor;
	sidebar->touch_active=sidebar->touch_dragged=0;
	sidebar_scroll_to_cursor(sidebar);
}

static int activate(UiSectionsSidebar *sidebar) {
	int selected = sidebar_section_at(sidebar, sidebar->cursor);
	sidebar->open = 0;
	if (selected == UI_SECTION_ONLINE_AUDIO) ui_online_audio_set_home(0);
	if (selected == UI_SECTION_LOCAL_MEDIA) ui_home_tab_set(HOME_LIBRARY);
	return selected == sidebar->active && selected != UI_SECTION_ONLINE_AUDIO && selected!=UI_SECTION_LOCAL_MEDIA ? UI_SECTION_NONE : selected;
}

int ui_sections_sidebar_handle_buttons(UiSectionsSidebar *sidebar,
	                                   unsigned int *pressed,
	                                   unsigned int held_buttons,
	                                   unsigned char analog_y) {
	if (!sidebar || !pressed) return UI_SECTION_NONE;
	int requested=ui_sections_take_requested();
	if(requested!=UI_SECTION_NONE){*pressed=0;return requested;}
	sync_live_slots(sidebar);
	if (*pressed & SCE_CTRL_LTRIGGER) {
		sidebar->open = !sidebar->open;
        if(sidebar->open)ui_theme_scene_enter(UI_SCENE_MENU,"sections");
#ifdef VMD_ENABLE_PAF_NATIVE
		if (sidebar->open && ui_theme_id() == VT_UI_THEME_LEGACY_VITA)
			ui_native_vita_prepare();
#endif
		if(sidebar->open) {
			sidebar->cursor=sidebar_active_slot(sidebar);
			sidebar->focus_cursor=(float)(sidebar->audio_layout_page?sidebar->audio_layout_cursor:sidebar->cursor);
			sidebar_scroll_to_cursor(sidebar);sidebar->scroll=sidebar->scroll_target;sidebar->scroll_velocity=0;
		}
		sidebar->touch_active=sidebar->touch_dragged=0;
		sidebar->repeat_button = 0;
		*pressed &= ~SCE_CTRL_LTRIGGER;
		return UI_SECTION_NONE;
	}
	if (!sidebar->open) return UI_SECTION_NONE;
	if(sidebar->audio_layout_available && (*pressed & (SCE_CTRL_LEFT|SCE_CTRL_RIGHT))) {
		sidebar->audio_layout_page=!!(*pressed & SCE_CTRL_RIGHT);
		sidebar->focus_cursor=(float)(sidebar->audio_layout_page?sidebar->audio_layout_cursor:sidebar->cursor);
		*pressed=0; return UI_SECTION_NONE;
	}
	UiNavRepeat repeat = { sidebar->repeat_button, sidebar->repeat_next_us };
	unsigned int navigated = ui_nav_repeat_update(
	    &repeat, *pressed, held_buttons, 128, analog_y,
	    SCE_CTRL_UP | SCE_CTRL_DOWN |
	        ((!vt_ui_theme_legacy_layout(ui_theme_id()) && !sidebar->audio_layout_available)
	             ? SCE_CTRL_LEFT | SCE_CTRL_RIGHT
	             : 0));
	sidebar->repeat_button = repeat.direction;
	sidebar->repeat_next_us = repeat.repeat_next_us;
	if(sidebar->audio_layout_available && sidebar->audio_layout_page) {
		if((navigated&SCE_CTRL_UP) && sidebar->audio_layout_cursor>0)sidebar->audio_layout_cursor--;
		if((navigated&SCE_CTRL_DOWN) && sidebar->audio_layout_cursor<2)sidebar->audio_layout_cursor++;
		int result=(*pressed&SCE_CTRL_CROSS)?UI_SECTION_AUDIO_LAYOUT_BASE+sidebar->audio_layout_cursor:UI_SECTION_NONE;
		if(*pressed&SCE_CTRL_CIRCLE)sidebar->open=0;
		*pressed=0;return result;
	}
	int slot_count = UI_SECTION_COUNT + sidebar->player_available + sidebar->download_available;
	if (!sidebar_vertical() && !sidebar->audio_layout_available) {
		if (navigated)
			sidebar->cursor = sidebar_neighbour(sidebar, navigated);
	} else {
		if ((navigated & SCE_CTRL_UP) && sidebar->cursor > 0)
			sidebar->cursor--;
		if ((navigated & SCE_CTRL_DOWN) && sidebar->cursor + 1 < slot_count)
			sidebar->cursor++;
	}

	if(navigated || (*pressed&SCE_CTRL_CROSS)) {
		sidebar->touch_active=sidebar->touch_dragged=0;
		sidebar_scroll_to_cursor(sidebar);
	}
	int selected = (*pressed & SCE_CTRL_CROSS) ? activate(sidebar) : UI_SECTION_NONE;
	if (*pressed & SCE_CTRL_CIRCLE) sidebar->open = 0;
	*pressed &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT |
	              SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE |
	              SCE_CTRL_SQUARE | SCE_CTRL_START | SCE_CTRL_RTRIGGER);
	return selected;
}

int ui_sections_sidebar_handle_touch(UiSectionsSidebar *sidebar,
                                     unsigned int flags,int x,int y) {
    if(!sidebar||!sidebar->open)return UI_SECTION_NONE;
    sync_live_slots(sidebar);
	int ox = (int)sidebar_offset(sidebar), legacy = vt_ui_theme_legacy_layout(ui_theme_id());
	int left = ox + (legacy ? 0 : 48), right = ox + (legacy ? SIDEBAR_WIDTH : 912);
	int in_body = x >= left && x < right && y >= sidebar_view_top() && y < sidebar_view_bottom();
	if (!sidebar->audio_layout_page && sidebar_vertical()) {
		if ((flags & UI_TOUCH_EVENT_DOWN) && in_body) {
			sidebar->touch_active = 1;
			sidebar->touch_dragged = 0;
			sidebar->touch_start_y=y;sidebar->touch_start_scroll=sidebar->scroll;
            sidebar->scroll_target=sidebar->scroll;sidebar->scroll_velocity=0;
		}
		if (sidebar->touch_active &&
		    (flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD | UI_TOUCH_EVENT_UP))) {
			int delta=sidebar->touch_start_y-y;
            if(abs(delta)>=8)sidebar->touch_dragged=1;
            if(sidebar->touch_dragged) {
                sidebar->scroll=sidebar->scroll_target=sidebar_scroll_clamp(sidebar,sidebar->touch_start_scroll+delta);
                sidebar->scroll_velocity=0;
            }
		}
		if((flags&UI_TOUCH_EVENT_UP)&&sidebar->touch_active) {
            sidebar->touch_active=0;
            if(sidebar->touch_dragged){sidebar_focus_visible(sidebar);sidebar->touch_dragged=0;return UI_SECTION_NONE;}
        }
	}
	if (!(flags & UI_TOUCH_EVENT_TAP))
		return UI_SECTION_NONE;
	if (sidebar->audio_layout_available) {
		UiThemeRect tabs=legacy?(UiThemeRect){ox+20,54,384,40}:(UiThemeRect){ox+64,94,584,44};
        if(ui_touch_hit_rect(x,y,tabs.x,tabs.y,tabs.w,tabs.h)) {
            sidebar->audio_layout_page=x>=ox+(legacy?209:320);
            sidebar->focus_cursor=(float)(sidebar->audio_layout_page?sidebar->audio_layout_cursor:sidebar->cursor);
            sidebar->touch_active=sidebar->touch_dragged=0;return UI_SECTION_NONE;
        }
        if(sidebar->audio_layout_page) {
            for(int i=0;i<3;i++) {
                UiThemeRect r=legacy?(UiThemeRect){14,177+i*91,396,79}:sidebar_layout_rect(i);
                if(ui_touch_hit_rect(x,y,ox+r.x,r.y,r.w,r.h)) {
                    sidebar->audio_layout_cursor=i;return UI_SECTION_AUDIO_LAYOUT_BASE+i;
                }
            }
        }
	}
	if(!sidebar->audio_layout_page) {
		for (int down = 0; sidebar_vertical() && down < 2; down++) {
			int bx = (legacy ? 318 : 806) + down * (legacy ? 48 : 50);
			if (ui_touch_hit_rect(x, y, ox + bx, 494, 36, 34)) {
				float target=sidebar_scroll_clamp(sidebar,sidebar->scroll_target+
                    (down?1:-1)*(sidebar_view_bottom()-sidebar_view_top())*.72f);
                if(fabsf(target-sidebar->scroll_target)>.5f) {
                    sidebar->scroll_target=target;sidebar_focus_visible(sidebar);
                }
                return UI_SECTION_NONE;
			}
		}
		if (in_body)
			for (int i = 0;
			     i < UI_SECTION_COUNT + sidebar->player_available + sidebar->download_available;
			     i++) {
				UiThemeRect r = sidebar_slot_rect(sidebar, i);
				if (sidebar_rect_visible(r) && ui_touch_hit_rect(x, y, ox + r.x, r.y, r.w, r.h)) {
					sidebar->cursor = i;
					return activate(sidebar);
				}
			}
	}
	if(legacy?x>=ox+SIDEBAR_WIDTH:!ui_touch_hit_rect(x,y,ox+36,70,888,464))sidebar->open=0;
    return UI_SECTION_NONE;
}

void ui_sections_sidebar_tick(UiSectionsSidebar *sidebar) {
	if (!sidebar) return;
	sync_live_slots(sidebar);
	uint64_t now = sceKernelGetProcessTimeWide();
	uint64_t elapsed = sidebar->animation_last_us &&
	                   now > sidebar->animation_last_us
	                 ? now - sidebar->animation_last_us : 16667ULL;
	if (elapsed > 50000ULL) elapsed = 50000ULL;
	sidebar->animation_last_us = now;
	if (vt_preferences_reduce_motion())
		sidebar->animation = sidebar->open ? 1.0f : 0.0f;
	else {
		float alpha = (float)elapsed / (ui_theme_motion_profile(ui_theme_id()).menu_us + (float)elapsed);
		sidebar->animation += ((sidebar->open ? 1.0f : 0.0f) -
		                       sidebar->animation) * alpha;
	}
	if(sidebar->scroll_theme!=ui_theme_id()) {
		sidebar->scroll_theme=ui_theme_id();sidebar->scroll=sidebar->scroll_target=0;
		ui_focus_motion_reset(&sidebar->fixed_focus);
		sidebar_scroll_to_cursor(sidebar);sidebar->scroll=sidebar->scroll_target;sidebar->scroll_velocity=0;
		sidebar->touch_active=sidebar->touch_dragged=0;
	}
	sidebar->scroll_target=sidebar_scroll_clamp(sidebar,sidebar->scroll_target);
	if(vt_preferences_reduce_motion()||sidebar->touch_active) {
		sidebar->scroll=sidebar->scroll_target;sidebar->scroll_velocity=0;
	}else {
		float omega=5.0f/ui_theme_motion_profile(ui_theme_id()).scroll_seconds;
		ui_page_spring(&sidebar->scroll,&sidebar->scroll_velocity,sidebar->scroll_target,(float)elapsed/1000000.0f,omega);
		sidebar->scroll=sidebar_scroll_clamp(sidebar,sidebar->scroll);
		if(!isfinite(sidebar->scroll_velocity))sidebar->scroll_velocity=0;
	}
	if(!sidebar->open)sidebar->touch_active=sidebar->touch_dragged=0;
	if (!sidebar->open && sidebar->animation < 0.025f) sidebar->animation = 0.0f;
    if(ui_theme_id()==VT_UI_THEME_LEGACY_VITA) {
        float y=sidebar->audio_layout_page ? 177+sidebar->audio_layout_cursor*91
            : sidebar_row_y(sidebar,(float)sidebar->cursor)-sidebar->scroll;
        ui_focus_motion_tick(&sidebar->fixed_focus,14,y,396,
            sidebar->audio_layout_page?79:sidebar_row_height());
    } else if (!sidebar_vertical() && !sidebar->audio_layout_page) {
		UiThemeRect rect = sidebar_content_rect(sidebar, sidebar->cursor);
		ui_focus_motion_tick(&sidebar->fixed_focus, rect.x, rect.y, rect.w, rect.h);
	}
	float target=(float)(sidebar->audio_layout_page?sidebar->audio_layout_cursor:sidebar->cursor);
	if (vt_preferences_reduce_motion())
		sidebar->focus_cursor = target;
	else {
		float alpha = (float)elapsed / (50000.0f + (float)elapsed);
		sidebar->focus_cursor +=
		    (target - sidebar->focus_cursor) * alpha;
	}
}

void ui_sections_sidebar_draw(const UiSectionsSidebar *sidebar) {
	if (!sidebar) return;
	int cursor = sidebar->cursor;
	float animation = sidebar->animation;
	float focus_cursor = sidebar->open ? sidebar->focus_cursor : -1.0f;
	const char *items[] = {
		vt_i18n_str(VT_STR_SECTIONS_HOME),
		vt_i18n_str(VT_STR_SECTIONS_NETWORK),
		vt_i18n_str(VT_STR_SECTIONS_ONLINE_AUDIO),
		vt_i18n_str(VT_STR_IPTV_TITLE),
		vt_i18n_str(VT_STR_SECTIONS_SETTINGS),
		vt_i18n_str(VT_STR_SECTIONS_GUIDE),
		vt_i18n_str(VT_STR_SECTIONS_ABOUT),
		"Navidrome", "Immich", "Jellyfin"
	};
	if (!vt_ui_theme_legacy_layout(ui_theme_id())) {
		sidebar_themed_draw(sidebar, items);
		return;
	}
	float ox = sidebar_offset(sidebar);
    int native = 0;
#ifdef VMD_ENABLE_PAF_NATIVE
    native = ui_native_vita_active();
    int native_scope = ui_native_vita_scope(UI_NATIVE_SCOPE_SECTIONS);
	if (ui_theme_id() == VT_UI_THEME_LEGACY_VITA && ui_native_vita_error())
		ui_text_line(446, 508, 490,
		             vt_i18n_current_language() == VT_LANG_IT ? "PAF non disponibile: layout Legacy"
		                                                      : "PAF unavailable: Legacy layout",
		             VT_THEME_WARNING, UI_FONT_SMALL);
#endif
	vita2d_set_clip_rectangle(0,0,960,544);vita2d_enable_clipping();
	/* The signal index is modal and full height. The veil follows the same
	 * interpolation so the page recedes as the rail enters. */
	if (animation > 0.01f)
		vita2d_draw_rectangle(ox + SIDEBAR_WIDTH, 0,
		                      960 - (ox + SIDEBAR_WIDTH), SCREEN_HEIGHT,
		                      RGBA8(0, 0, 0, (unsigned int)(150.0f * animation)));
    if (native)
        ui_panel(ox, 0, SIDEBAR_WIDTH, SCREEN_HEIGHT, VT_THEME_BG_SOFT, VT_THEME_BORDER_DIM, 0);
    else {
	vita2d_draw_rectangle(ox, 0, SIDEBAR_WIDTH, SCREEN_HEIGHT,
	                      VT_THEME_BG_SOFT);
	vita2d_draw_rectangle(ox + SIDEBAR_WIDTH - 2, 0, 2, SCREEN_HEIGHT,
	                      VT_THEME_SPECTRAL);
	vita2d_draw_rectangle(ox + SIDEBAR_WIDTH - 7, 0, 4, SCREEN_HEIGHT,
	                      VT_THEME_SIGNAL_A(42));
    }
	vita2d_font *font = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	/* Keep decoration out of the navigation labels and moving focus band. */
#ifdef VMD_ENABLE_PAF_NATIVE
    if (native) ui_native_vita_button(ox + 20, 14, 44, 32, "L1", 0);
    else
#endif
	vita2d_draw_rectangle(ox + 20, 18, 42, 26, VT_THEME_SURFACE_RAISED);
    if (!native) vita2d_draw_rectangle(ox + 20, 18, 3, 26, VT_THEME_SIGNAL_BRIGHT);
	if (small) {
		int key_width = ui_font_text_width(small, UI_FONT_SMALL, "L1");
        if (!native) ui_font_draw_text(small, (int)ox + 41 - key_width / 2, 38,
		                  VT_THEME_TEXT, UI_FONT_SMALL, "L1");
		ui_font_draw_text(small, (int)ox + 76, 38, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, "VitaMediaDeck");
	}
	if (!font) goto sidebar_draw_done;
	ui_font_draw_text(font, (int)ox + 28, 80, sidebar->audio_layout_page?VT_THEME_TEXT_MUTED:VT_THEME_TEXT,
	                      UI_FONT_BODY, vt_i18n_str(VT_STR_SECTIONS_TITLE));
	if(sidebar->audio_layout_available) {
		ui_font_draw_text(font,(int)ox+209,80,sidebar->audio_layout_page?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT_MUTED,
		    UI_FONT_BODY,vt_i18n_str(VT_STR_MUSIC_LAYOUT_PLAYER));
		vita2d_draw_rectangle(ox+(sidebar->audio_layout_page?209:28),89,90,2,VT_THEME_SIGNAL_BRIGHT);
	}
	if(sidebar->audio_layout_available && sidebar->audio_layout_page) {
		ui_font_draw_text(font,(int)ox+20,151,VT_THEME_TEXT,UI_FONT_BODY,vt_i18n_str(VT_STR_MUSIC_LAYOUT_TITLE));
		const VtStringId labels[]={VT_STR_MUSIC_LAYOUT_CENTER,VT_STR_MUSIC_LAYOUT_LEFT,VT_STR_MUSIC_LAYOUT_RIGHT};
		for(int i=0;i<3;i++) {
#ifdef VMD_ENABLE_PAF_NATIVE
            if (native && ui_native_vita_button(ox+14,177+i*91,396,79,
                vt_i18n_str(labels[i]),i==sidebar->audio_layout_selected)) continue;
#endif
			vita2d_draw_rectangle(ox+14,177+i*91,396,79,VT_THEME_SURFACE);
			if(i==sidebar->audio_layout_selected)vita2d_draw_rectangle(ox+28,210+i*91,8,8,VT_THEME_SIGNAL_BRIGHT);
			ui_font_draw_text(font,(int)ox+48,222+i*91,VT_THEME_TEXT,UI_FONT_BODY,vt_i18n_str(labels[i]));
		}
        if(sidebar->open){
            UiFocusMotion focus=native?sidebar->fixed_focus:(UiFocusMotion){.x=14,.y=177+sidebar->focus_cursor*91,.width=396,.height=79,.initialized=1};
            focus.x+=ox;ui_focus_outline_draw(&focus);
        }
        if (native) ui_panel(ox+14,448,396,88,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
		if(small){
			ui_font_draw_text(small,(int)ox+20,478,VT_THEME_TEXT_MUTED,UI_FONT_SMALL,vt_i18n_str(VT_STR_MUSIC_LAYOUT_APPLIES));
			ui_font_draw_text(small,(int)ox+20,514,VT_THEME_TEXT_MUTED,UI_FONT_SMALL,vt_i18n_str(VT_STR_MUSIC_LAYOUT_UNCHANGED));
		}
        goto sidebar_draw_done;
	}
	if(!sidebar_clip_body(ox)) goto sidebar_draw_done;
	int has_focus = focus_cursor >= 0.0f;
	if (has_focus && !native) {
		vita2d_draw_rectangle(ox + ITEM_X, sidebar_row_y(sidebar, focus_cursor)-sidebar->scroll,
		                      ITEM_W, sidebar_row_height(), VT_THEME_SURFACE_FOCUS);
		vita2d_draw_rectangle(ox + ITEM_X, sidebar_row_y(sidebar, focus_cursor)-sidebar->scroll,
		                      ITEM_W, 2, VT_THEME_FOCUS);
		vita2d_draw_rectangle(ox + ITEM_X, sidebar_row_y(sidebar, focus_cursor)-sidebar->scroll,
		                      3, sidebar_row_height(), VT_THEME_FOCUS);
	}
	int slot_count = UI_SECTION_COUNT + sidebar->player_available +
	                 sidebar->download_available;
	for (int slot = 0; slot < slot_count; slot++) {
        int section = sidebar_section_at(sidebar, slot);
        const char *label = section == UI_SECTION_PLAYER ? vt_i18n_str(VT_STR_SECTIONS_PLAYER)
                          : section == UI_SECTION_DOWNLOAD ? vt_i18n_str(VT_STR_SECTIONS_DOWNLOAD)
                          : items[section];
		UiThemeRect rect=sidebar_slot_rect(sidebar,slot);
		if(!sidebar_rect_visible(rect))continue;
#ifdef VMD_ENABLE_PAF_NATIVE
        /* Native rows use the firmware's nine-slice button and label, with
         * the same existing scroll/touch rectangles. Edge-clipped rows keep
         * the adapter's clipped text path until the whole target fits. */
        if (native && ui_native_vita_button_fade(ox + rect.x, rect.y, rect.w, rect.h,
                                                label, has_focus && slot == cursor, animation))
            continue;
#endif
		unsigned int color = has_focus && slot == cursor ? VT_THEME_TEXT
		                                      : VT_THEME_TEXT_MUTED;
		ui_font_draw_text(font, (int)ox + 34,
		                  rect.y + 28, color,
		                      UI_FONT_BODY, label);
	}
    if(native && sidebar->open) {
        UiFocusMotion focus=sidebar->fixed_focus;
        if(!focus.initialized) {
            UiThemeRect r=sidebar_slot_rect(sidebar,cursor);
            focus=(UiFocusMotion){.x=r.x,.y=r.y,.width=r.w,.height=r.h,.initialized=1};
        }
        focus.x+=ox;ui_focus_outline_draw(&focus);
    }
    sidebar_group_headings(sidebar,ox);
    vita2d_set_clip_rectangle(0,0,960,544);
    sidebar_scroll_chrome(sidebar,ox);
sidebar_draw_done:
    vita2d_disable_clipping();
#ifdef VMD_ENABLE_PAF_NATIVE
    ui_native_vita_scope(native_scope);
#endif
}
