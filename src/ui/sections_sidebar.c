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
static int sidebar_view_top(void) { return 104; }
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
static int sidebar_vertical(void) { return 1; }
static UiThemeRect sidebar_content_rect(const UiSectionsSidebar *sidebar,int slot){
 return (UiThemeRect){ITEM_X,(int)sidebar_row_y(sidebar,(float)slot),ITEM_W,sidebar_row_height()};
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
static float sidebar_offset(const UiSectionsSidebar *sidebar){return -SIDEBAR_WIDTH*(1-sidebar->animation);}
static int sidebar_clip_body(float ox){
 int left=fmaxf(0,ox),right=fminf(960,ox+SIDEBAR_WIDTH);
 if(right<=left)return 0;
 vita2d_set_clip_rectangle(left,sidebar_view_top(),right,sidebar_view_bottom());vita2d_enable_clipping();return 1;
}
static void sidebar_group_headings(const UiSectionsSidebar *s,float ox){
 int live=s->player_available+s->download_available;
 const int starts[]={0,live,live+SIDEBAR_CONTENT_COUNT};
 const VtStringId labels[]={VT_STR_SECTIONS_GROUP_LIVE,VT_STR_SECTIONS_GROUP_CONTENT,VT_STR_SECTIONS_GROUP_APP};
 for(int group=live?0:1;group<3;group++){
  UiThemeRect r=sidebar_slot_rect(s,starts[group]);int y=r.y-14;
  if(y-UI_FONT_SMALL>=sidebar_view_top()&&y+4<=sidebar_view_bottom())
   ui_menu_text(ox+34,y,350,vt_i18n_str(labels[group]),UI_MENU_ACCENT,UI_FONT_SMALL);
 }
}
static void sidebar_scroll_chrome(const UiSectionsSidebar *s,float ox){
 ui_menu_text(ox+28,503,272,vt_i18n_str(VT_STR_SECTIONS_SCROLL_HINT),UI_MENU_MUTED,UI_FONT_SMALL);
 ui_menu_text(ox+28,528,272,vt_i18n_str(s->audio_layout_available?VT_STR_MUSIC_LAYOUT_HINT:VT_STR_SECTIONS_FOOTER_HINT),UI_MENU_MUTED,UI_FONT_SMALL);
 float maximum=sidebar_scroll_max(s);if(maximum<=0)return;
 float top=sidebar_view_top()+6,height=sidebar_view_bottom()-sidebar_view_top()-12;
 float thumb=fmaxf(30,height*height/(height+maximum));
 vita2d_draw_rectangle(ox+412,top,2,height,VT_THEME_BORDER_DIM);
 vita2d_draw_rectangle(ox+412,top+(height-thumb)*s->scroll/maximum,2,thumb,UI_MENU_ACCENT);
 for(int down=0;down<2;down++){
  float cx=ox+336+down*48,cy=511,sign=down?1:-1;
  unsigned ink=(down?s->scroll_target<maximum-.5f:s->scroll_target>.5f)?UI_MENU_TEXT:UI_MENU_FAINT;
  vita2d_draw_line(cx-6,cy-sign*3,cx,cy+sign*3,ink);vita2d_draw_line(cx,cy+sign*3,cx+6,cy-sign*3,ink);
 }
}
static UiThemeRect sidebar_layout_rect(int i){return (UiThemeRect){20,177+i*91,384,79};}

void ui_sections_sidebar_init(UiSectionsSidebar *sidebar, int active) {
	if (!sidebar) return;
    sidebar->touch_last_us=sidebar->analog_last_us=0;sidebar->touch_last_y=0;sidebar->scroll_coasting=0;
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
	sidebar->touch_active=sidebar->touch_dragged=sidebar->scroll_coasting=0;
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
		sidebar->touch_active=sidebar->touch_dragged=sidebar->scroll_coasting=0;
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
	    &repeat, *pressed, held_buttons, 128, sidebar_vertical()&&!sidebar->audio_layout_page?128:analog_y,
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
    uint64_t input_now=sceKernelGetProcessTimeWide();
    float input_dt=sidebar->analog_last_us&&input_now>sidebar->analog_last_us?(input_now-sidebar->analog_last_us)/1000000.f:1.f/60;
    if(input_dt>.05f)input_dt=.05f;
    sidebar->analog_last_us=input_now;
    if(sidebar_vertical()&&!sidebar->audio_layout_page&&abs((int)analog_y-128)>32){
        float axis=(analog_y-128)/127.f;
        sidebar->scroll=sidebar->scroll_target=sidebar_scroll_clamp(sidebar,sidebar->scroll+axis*600*input_dt);
        sidebar->scroll_velocity=0;sidebar->scroll_coasting=0;sidebar_focus_visible(sidebar);
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
		sidebar->touch_active=sidebar->touch_dragged=sidebar->scroll_coasting=0;
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
	int ox = (int)sidebar_offset(sidebar), legacy = 1;
	int left = ox + (legacy ? 0 : 48), right = ox + (legacy ? SIDEBAR_WIDTH : 912);
	int in_body = x >= left && x < right && y >= sidebar_view_top() && y < sidebar_view_bottom();
	if (!sidebar->audio_layout_page && sidebar_vertical()) {
		if ((flags & UI_TOUCH_EVENT_DOWN) && in_body) {
			sidebar->touch_active = 1;
			sidebar->touch_dragged = 0;
			sidebar->touch_start_y=sidebar->touch_last_y=y;sidebar->touch_last_us=sceKernelGetProcessTimeWide();sidebar->touch_start_scroll=sidebar->scroll;sidebar->scroll_coasting=0;
            sidebar->scroll_target=sidebar->scroll;sidebar->scroll_velocity=0;
		}
		if (sidebar->touch_active &&
		    (flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD | UI_TOUCH_EVENT_UP))) {
			int delta=sidebar->touch_start_y-y;
            if(abs(delta)>=8)sidebar->touch_dragged=1;
            if(sidebar->touch_dragged) {
                uint64_t now=sceKernelGetProcessTimeWide();
                float dt=now>sidebar->touch_last_us?(now-sidebar->touch_last_us)/1000000.f:1.f/60;
                float before=sidebar->scroll;
                sidebar->scroll=sidebar->scroll_target=sidebar_scroll_clamp(sidebar,sidebar->touch_start_scroll+delta);
                if(y!=sidebar->touch_last_y)sidebar->scroll_velocity=sidebar->scroll_velocity*.35f+(sidebar->scroll-before)/fmaxf(.001f,dt)*.65f;
                else if(flags&UI_TOUCH_EVENT_HOLD)sidebar->scroll_velocity*=expf(-18*dt);
                sidebar->touch_last_y=y;sidebar->touch_last_us=now;
            }
		}
		if((flags&UI_TOUCH_EVENT_UP)&&sidebar->touch_active) {
            sidebar->touch_active=0;
            if(sidebar->touch_dragged){sidebar->scroll_coasting=!vt_preferences_reduce_motion()&&fabsf(sidebar->scroll_velocity)>8;sidebar_focus_visible(sidebar);sidebar->touch_dragged=0;return UI_SECTION_NONE;}
        }
	}
	if (!(flags & UI_TOUCH_EVENT_TAP))
		return UI_SECTION_NONE;
	if (sidebar->audio_layout_available) {
		UiThemeRect tabs=legacy?(UiThemeRect){ox+20,54,384,40}:(UiThemeRect){ox+64,94,584,44};
        if(ui_touch_hit_rect(x,y,tabs.x,tabs.y,tabs.w,tabs.h)) {
            sidebar->audio_layout_page=x>=ox+(legacy?209:320);
            sidebar->focus_cursor=(float)(sidebar->audio_layout_page?sidebar->audio_layout_cursor:sidebar->cursor);
            sidebar->touch_active=sidebar->touch_dragged=sidebar->scroll_coasting=0;return UI_SECTION_NONE;
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
		sidebar->touch_active=sidebar->touch_dragged=sidebar->scroll_coasting=0;
	}
	sidebar->scroll_target=sidebar_scroll_clamp(sidebar,sidebar->scroll_target);
	if(vt_preferences_reduce_motion()||sidebar->touch_active) {
		sidebar->scroll=sidebar->scroll_target;
        if(!sidebar->touch_active)sidebar->scroll_velocity=0;
	}else if(sidebar->scroll_coasting){
        float dt=elapsed/1000000.f,decay=expf(-7*dt);
        sidebar->scroll=sidebar->scroll_target=sidebar_scroll_clamp(sidebar,sidebar->scroll+sidebar->scroll_velocity*(1-decay)/7);
        sidebar->scroll_velocity*=decay;
        if(fabsf(sidebar->scroll_velocity)<8||sidebar->scroll==0||sidebar->scroll==sidebar_scroll_max(sidebar))sidebar->scroll_coasting=0;
        sidebar_focus_visible(sidebar);
    }else {
		float omega=5.0f/ui_theme_motion_profile(ui_theme_id()).scroll_seconds;
		ui_page_spring(&sidebar->scroll,&sidebar->scroll_velocity,sidebar->scroll_target,(float)elapsed/1000000.0f,omega);
		sidebar->scroll=sidebar_scroll_clamp(sidebar,sidebar->scroll);
		if(!isfinite(sidebar->scroll_velocity))sidebar->scroll_velocity=0;
	}
	if(!sidebar->open)sidebar->touch_active=sidebar->touch_dragged=sidebar->scroll_coasting=0;
	if (!sidebar->open && sidebar->animation < 0.025f) sidebar->animation = 0.0f;
    {
        float y=sidebar->audio_layout_page?177+sidebar->audio_layout_cursor*91:
            sidebar_row_y(sidebar,(float)sidebar->cursor)-sidebar->scroll;
        ui_focus_motion_tick(&sidebar->fixed_focus,20,y,384,sidebar->audio_layout_page?79:sidebar_row_height());
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

void ui_sections_sidebar_draw(const UiSectionsSidebar *s){
 if(!s||s->animation<=.01f)return;
#ifdef VMD_ENABLE_PAF_NATIVE
 int native_scope=ui_native_vita_scope(UI_NATIVE_SCOPE_SECTIONS);
#endif
 float ox=sidebar_offset(s);
 vita2d_draw_rectangle(ox+SIDEBAR_WIDTH,0,960-(ox+SIDEBAR_WIDTH),544,RGBA8(0,0,0,(unsigned)(150*s->animation)));
 ui_menu_surface(ox,0,SIDEBAR_WIDTH,544,1);
 vita2d_draw_rectangle(ox+SIDEBAR_WIDTH-1,0,1,544,ui_theme_alpha(UI_MENU_ACCENT,160));
 vita2d_set_clip_rectangle((int)fmaxf(0,ox),0,(int)fminf(960,ox+SIDEBAR_WIDTH),544);vita2d_enable_clipping();
 ui_menu_text(ox+28,38,48,UI_PAD_L1,UI_MENU_TEXT,UI_FONT_SMALL);
 ui_menu_text(ox+90,38,300,"VitaMediaDeck",UI_MENU_MUTED,UI_FONT_SMALL);
 ui_menu_text(ox+28,80,s->audio_layout_available?170:360,vt_i18n_str(VT_STR_SECTIONS_TITLE),UI_MENU_TEXT,UI_FONT_BODY);
 if(s->audio_layout_available)
  ui_menu_text(ox+209,80,190,vt_i18n_str(VT_STR_MUSIC_LAYOUT_PLAYER),s->audio_layout_page?UI_MENU_ACCENT:UI_MENU_MUTED,UI_FONT_BODY);
 const char *items[]={vt_i18n_str(VT_STR_SECTIONS_HOME),vt_i18n_str(VT_STR_SECTIONS_NETWORK),
  vt_i18n_str(VT_STR_SECTIONS_ONLINE_AUDIO),vt_i18n_str(VT_STR_IPTV_TITLE),vt_i18n_str(VT_STR_SECTIONS_SETTINGS),
  vt_i18n_str(VT_STR_SECTIONS_GUIDE),vt_i18n_str(VT_STR_SECTIONS_ABOUT),"Navidrome","Immich","Jellyfin"};
 if(sidebar_clip_body(ox)){
  if(s->open){UiFocusMotion f=s->fixed_focus;
   if(!f.initialized){UiThemeRect r=s->audio_layout_page?sidebar_layout_rect(s->audio_layout_cursor):sidebar_slot_rect(s,s->cursor);f=(UiFocusMotion){.x=r.x,.y=r.y,.width=r.w,.height=r.h,.initialized=1};}
   f.x+=ox;ui_menu_focus_draw(&f);
  }
  if(s->audio_layout_available&&s->audio_layout_page){
   const VtStringId labels[]={VT_STR_MUSIC_LAYOUT_CENTER,VT_STR_MUSIC_LAYOUT_LEFT,VT_STR_MUSIC_LAYOUT_RIGHT};
   for(int i=0;i<3;i++){
    UiThemeRect r=sidebar_layout_rect(i);r.x+=ox;
    ui_menu_row(r.x,r.y,r.w,r.h,0,1);
    ui_menu_text(r.x+14,r.y+45,r.w-54,vt_i18n_str(labels[i]),UI_MENU_TEXT,UI_FONT_BODY);
    if(i==s->audio_layout_selected)ui_menu_text(r.x+r.w-36,r.y+45,30,"✓",UI_MENU_ACCENT,UI_FONT_BODY);
   }
  }else{
   int count=UI_SECTION_COUNT+s->player_available+s->download_available;
   for(int i=0;i<count;i++){
    UiThemeRect r=sidebar_slot_rect(s,i);if(!sidebar_rect_visible(r))continue;r.x+=ox;
    int section=sidebar_section_at(s,i);
    const char *label=section==UI_SECTION_PLAYER?vt_i18n_str(VT_STR_SECTIONS_PLAYER):section==UI_SECTION_DOWNLOAD?vt_i18n_str(VT_STR_SECTIONS_DOWNLOAD):items[section];
    ui_menu_row(r.x,r.y,r.w,r.h,0,1);
    ui_menu_text(r.x+14,r.y+29,r.w-56,label,UI_MENU_TEXT,UI_FONT_BODY);
    if(section==s->active)ui_menu_text(r.x+r.w-34,r.y+29,28,"✓",UI_MENU_ACCENT,UI_FONT_SMALL);
   }
   sidebar_group_headings(s,ox);
  }
 }
 vita2d_set_clip_rectangle((int)fmaxf(0,ox),0,(int)fminf(960,ox+SIDEBAR_WIDTH),544);vita2d_enable_clipping();
 if(!s->audio_layout_page)sidebar_scroll_chrome(s,ox);
 else ui_menu_text(ox+28,514,368,vt_i18n_str(VT_STR_MUSIC_LAYOUT_APPLIES),UI_MENU_MUTED,UI_FONT_SMALL);
 vita2d_disable_clipping();
#ifdef VMD_ENABLE_PAF_NATIVE
 ui_native_vita_scope(native_scope);
#endif
}
