#include "ui/sections_sidebar.h"

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
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui/components.h"
#include "ui/touch.h"

#define SCREEN_HEIGHT 544
#define SIDEBAR_WIDTH 424
#define ITEM_X 20
#define ITEM_Y 104
#define ITEM_W 384
#define ITEM_H 30
#define ITEM_STEP 34

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
        UI_SECTION_LOCAL_MEDIA, UI_SECTION_NETWORK, UI_SECTION_NAVIDROME,
        UI_SECTION_IMMICH, UI_SECTION_ONLINE_AUDIO, UI_SECTION_IPTV,
        UI_SECTION_SETTINGS, UI_SECTION_GUIDE, UI_SECTION_INFO
    };
    if (sidebar->player_available && slot == 0) return UI_SECTION_PLAYER;
    if (sidebar->download_available && slot == sidebar->player_available)
        return UI_SECTION_DOWNLOAD;
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
static float sidebar_row_y(const UiSectionsSidebar *sidebar, float slot) {
    if (ui_theme_id() != VT_UI_THEME_LEGACY) return ITEM_Y + slot * ITEM_STEP;
    int live = sidebar->player_available + sidebar->download_available;
    int count = UI_SECTION_COUNT + live;
    if (slot < 0) slot = 0;
    if (slot > count - 1) slot = count - 1;
    int index = (int)slot;
    float y = 114 + index * 28;
    if (live && index >= live) y += 36;
    if (index >= live + 6) y += 36;
    if (index + 1 < count) {
        int next_gap = (live && index + 1 == live) || index + 1 == live + 6;
        y += (slot - index) * (28 + (next_gap ? 36 : 0));
    }
    return y;
}
static int sidebar_row_height(void) {
    return ui_theme_id() == VT_UI_THEME_LEGACY ? 26 : ITEM_H;
}

static float sidebar_offset(const UiSectionsSidebar *sidebar) {
 return -(ui_theme_id()==VT_UI_THEME_LEGACY?SIDEBAR_WIDTH:960)*(1-sidebar->animation);
}
static UiThemeRect sidebar_slot_rect(const UiSectionsSidebar *sidebar,int slot) {
 if(ui_theme_id()==VT_UI_THEME_LEGACY)return (UiThemeRect){ITEM_X,(int)sidebar_row_y(sidebar,(float)slot),ITEM_W,sidebar_row_height()};
 int live=sidebar->player_available+sidebar->download_available;
 if(slot<live)return (UiThemeRect){64+slot*428,164,live==1?832:404,34};
 int index=slot-live,theme=ui_theme_id();UiThemeRect r;
 if(theme==VT_UI_THEME_GHOST_MESH){
  if(index<6){float a=-1.57079632679f+index*1.0471975512f;r=(UiThemeRect){(int)(350+cosf(a)*212)-96,(int)(342+sinf(a)*108)-24,192,48};}
  else r=(UiThemeRect){716,234+(index-6)*82,184,64};
 }else if(theme==VT_UI_THEME_DECK_90)r=(UiThemeRect){104,202+index*32,670,28};
 else if(theme==VT_UI_THEME_SIGNAL){
  if(index<6)r=(UiThemeRect){64+(index%2)*286,234+(index/2)*84,268,70};
  else r=(UiThemeRect){664,234+(index-6)*84,232,70};
 }else if(theme==VT_UI_THEME_APERTURE)
  r=(UiThemeRect){104,212+index*31,752,27};
 else if(theme==VT_UI_THEME_RELAY){
  int row=index/3,col=row%2?2-index%3:index%3;
  r=(UiThemeRect){56+col*290,220+row*90,260,72};
 }else r=(UiThemeRect){62+(index%3)*288,218+(index/3)*92,260,78};
 return ui_theme_scene_reveal(UI_SCENE_MENU,r,index);
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
 ui_theme_scene_model(UI_SCENE_MENU,ox+model_x,model_y,theme==VT_UI_THEME_APERTURE?210:theme==VT_UI_THEME_GHOST_MESH?120:theme==VT_UI_THEME_DECK_90?130:78,.55f,1);
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
 }else{
  int live=sidebar->player_available+sidebar->download_available;
  int heading=live?218:180;
  if(live)ui_text_line(ox+64,152,832,vt_i18n_str(VT_STR_SECTIONS_GROUP_LIVE),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  if(theme==VT_UI_THEME_GHOST_MESH||theme==VT_UI_THEME_SIGNAL)
  ui_text_line(ox+64,heading,548,vt_i18n_str(VT_STR_SECTIONS_GROUP_CONTENT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  if(theme==VT_UI_THEME_GHOST_MESH||theme==VT_UI_THEME_SIGNAL)
  ui_text_line(ox+(theme==VT_UI_THEME_GHOST_MESH?716:664),heading,220,vt_i18n_str(VT_STR_SECTIONS_GROUP_APP),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  int count=UI_SECTION_COUNT+live;
  for(int i=0;i<count;i++){
   UiThemeRect r=sidebar_slot_rect(sidebar,i);r.x+=(int)ox;int section=sidebar_section_at(sidebar,i);
   const char *label=section==UI_SECTION_PLAYER?vt_i18n_str(VT_STR_SECTIONS_PLAYER):section==UI_SECTION_DOWNLOAD?vt_i18n_str(VT_STR_SECTIONS_DOWNLOAD):items[section];
   int selected=sidebar->open&&i==sidebar->cursor;
   unsigned background=selected?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE;
   unsigned fg=selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT;
   if(theme==VT_UI_THEME_DECK_90&&selected){background=VT_THEME_SIGNAL_BRIGHT;fg=VT_THEME_BG;}
   ui_theme_scene_card(UI_SCENE_MENU,r,selected,i);
   if(theme!=VT_UI_THEME_APERTURE)ui_theme_fx_surface(r.x+3,r.y+2,r.w-6,r.h-4,background,selected);
   else vita2d_draw_rectangle(r.x+5,r.y+4,r.w-10,r.h-8,background);
   ui_text_line(r.x+18,r.y+(r.h<40?21:r.h>90?83:32),r.w-36,label,fg,
       theme==VT_UI_THEME_MOSAIC?UI_FONT_SMALL:UI_FONT_BODY);
   if(section==sidebar->active)vita2d_draw_rectangle(r.x+8,r.y+8,3,r.h-16,VT_THEME_COLD);
  }
  if(sidebar->open){
   float cursor=sidebar->focus_cursor;if(cursor<0)cursor=0;if(cursor>count-1)cursor=count-1;
   int a=(int)cursor,b=a+1<count?a+1:a;float t=cursor-a;
   UiThemeRect from=sidebar_slot_rect(sidebar,a),to=sidebar_slot_rect(sidebar,b);
   UiFocusMotion focus={.x=ox+from.x+(to.x-from.x)*t,.y=from.y+(to.y-from.y)*t,
    .width=from.w+(to.w-from.w)*t,.height=from.h+(to.h-from.h)*t,.initialized=1};ui_focus_outline_draw(&focus);
  }
 }
 ui_text_line(ox+64,516,832,vt_i18n_str(sidebar->audio_layout_available?VT_STR_MUSIC_LAYOUT_HINT:VT_STR_SECTIONS_FOOTER_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
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
}

static void sync_live_slots(UiSectionsSidebar *sidebar) {
	if (!sidebar) return;
	int player_available = vt_background_playback_can_resume_fullscreen();
	int download_available = vt_download_background_active();
	if (player_available == sidebar->player_available &&
	    download_available == sidebar->download_available) return;
	sidebar->player_available = player_available;
	sidebar->download_available = download_available;
	/* Availability can change while the drawer is open. Return focus to the
	 * current page instead of letting it jump to a different live action. */
	sidebar->cursor = sidebar_active_slot(sidebar);
	sidebar->focus_cursor = (float)sidebar->cursor;
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
		if (sidebar->open)
			sidebar->cursor = sidebar_active_slot(sidebar);
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
	    SCE_CTRL_UP | SCE_CTRL_DOWN | ((ui_theme_id()!=VT_UI_THEME_LEGACY&&!sidebar->audio_layout_available)?SCE_CTRL_LEFT|SCE_CTRL_RIGHT:0));
	sidebar->repeat_button = repeat.direction;
	sidebar->repeat_next_us = repeat.repeat_next_us;
	if(sidebar->audio_layout_available && sidebar->audio_layout_page) {
		if((navigated&SCE_CTRL_UP) && sidebar->audio_layout_cursor>0)sidebar->audio_layout_cursor--;
		if((navigated&SCE_CTRL_DOWN) && sidebar->audio_layout_cursor<2)sidebar->audio_layout_cursor++;
		int result=(*pressed&SCE_CTRL_CROSS)?UI_SECTION_AUDIO_LAYOUT_BASE+sidebar->audio_layout_cursor:UI_SECTION_NONE;
		if(*pressed&SCE_CTRL_CIRCLE)sidebar->open=0;
		*pressed=0;return result;
	}
    if(ui_theme_id()!=VT_UI_THEME_LEGACY&&!sidebar->audio_layout_available&&(navigated&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))){
        int count=UI_SECTION_COUNT+sidebar->player_available+sidebar->download_available;
        UiThemeRect current=sidebar_slot_rect(sidebar,sidebar->cursor);
        float best=1e9f;int next=sidebar->cursor,sign=(navigated&SCE_CTRL_LEFT)?-1:1;
        for(int i=0;i<count;i++)if(i!=sidebar->cursor){
            UiThemeRect rr=sidebar_slot_rect(sidebar,i);
            float dx=(rr.x+rr.w*.5f)-(current.x+current.w*.5f);
            float dy=(rr.y+rr.h*.5f)-(current.y+current.h*.5f);
            if(dx*sign>8){float score=fabsf(dx)+fabsf(dy)*2.5f;if(score<best){best=score;next=i;}}
        }
        sidebar->cursor=next;
    }
	if ((navigated & SCE_CTRL_UP) && sidebar->cursor > 0) sidebar->cursor--;
	int slot_count = UI_SECTION_COUNT + sidebar->player_available +
	                 sidebar->download_available;
	if ((navigated & SCE_CTRL_DOWN) && sidebar->cursor + 1 < slot_count)
		sidebar->cursor++;
	int selected = (*pressed & SCE_CTRL_CROSS) ? activate(sidebar) : UI_SECTION_NONE;
	if (*pressed & SCE_CTRL_CIRCLE) sidebar->open = 0;
	*pressed &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT |
	              SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE |
	              SCE_CTRL_SQUARE | SCE_CTRL_START | SCE_CTRL_RTRIGGER);
	return selected;
}

int ui_sections_sidebar_handle_touch(UiSectionsSidebar *sidebar,
	                                 unsigned int touch_flags, int x, int y) {
	if (!sidebar || !sidebar->open || !(touch_flags & UI_TOUCH_EVENT_TAP))
		return UI_SECTION_NONE;
	sync_live_slots(sidebar);
	int offset_x=(int)sidebar_offset(sidebar);
    if(ui_theme_id()!=VT_UI_THEME_LEGACY){
        if(sidebar->audio_layout_available&&ui_touch_hit_rect(x,y,offset_x+64,94,584,44)){
            sidebar->audio_layout_page=x>=offset_x+320;
            sidebar->focus_cursor=(float)(sidebar->audio_layout_page?sidebar->audio_layout_cursor:sidebar->cursor);return UI_SECTION_NONE;
        }
        if(sidebar->audio_layout_available&&sidebar->audio_layout_page){
            for(int i=0;i<3;i++){UiThemeRect r=sidebar_layout_rect(i);if(ui_touch_hit_rect(x,y,offset_x+r.x,r.y,r.w,r.h)){
                sidebar->audio_layout_cursor=i;return UI_SECTION_AUDIO_LAYOUT_BASE+i;}}
        }else for(int i=0;i<UI_SECTION_COUNT+sidebar->player_available+sidebar->download_available;i++){
            UiThemeRect r=sidebar_slot_rect(sidebar,i);
            if(ui_touch_hit_rect(x,y,offset_x+r.x,r.y,r.w,r.h)){sidebar->cursor=i;return activate(sidebar);}
        }
        if(!ui_touch_hit_rect(x,y,offset_x+36,70,888,464))sidebar->open=0;
        return UI_SECTION_NONE;
    }
	if(sidebar->audio_layout_available) {
		if(ui_touch_hit_rect(x,y,offset_x+20,54,384,40)) {
			sidebar->audio_layout_page=x>=offset_x+209;
			sidebar->focus_cursor=(float)(sidebar->audio_layout_page?sidebar->audio_layout_cursor:sidebar->cursor);
			return UI_SECTION_NONE;
		}
		if(sidebar->audio_layout_page) {
			for(int i=0;i<3;i++)if(ui_touch_hit_rect(x,y,offset_x+14,177+i*91,396,79)) {
				sidebar->audio_layout_cursor=i;return UI_SECTION_AUDIO_LAYOUT_BASE+i;
			}
			if(x>=offset_x+SIDEBAR_WIDTH)sidebar->open=0;
			return UI_SECTION_NONE;
		}
	}
	int slot_count = UI_SECTION_COUNT + sidebar->player_available +
	                 sidebar->download_available;
	for (int i = 0; i < slot_count; i++) {
		if (!ui_touch_hit_rect(x, y, offset_x + ITEM_X, (int)sidebar_row_y(sidebar, (float)i),
		                       ITEM_W, sidebar_row_height())) continue;
		sidebar->cursor = i;
		return activate(sidebar);
	}
	if (x >= offset_x + SIDEBAR_WIDTH) sidebar->open = 0;
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
		float alpha = (float)elapsed / (60000.0f + (float)elapsed);
		sidebar->animation += ((sidebar->open ? 1.0f : 0.0f) -
		                       sidebar->animation) * alpha;
	}
	if (!sidebar->open && sidebar->animation < 0.025f) sidebar->animation = 0.0f;
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
		"Navidrome", "Immich"
	};
    if(ui_theme_id()!=VT_UI_THEME_LEGACY){sidebar_themed_draw(sidebar,items);return;}
	float ox = -(float)SIDEBAR_WIDTH * (1.0f - animation);
	/* The signal index is modal and full height. The veil follows the same
	 * interpolation so the page recedes as the rail enters. */
	if (animation > 0.01f)
		vita2d_draw_rectangle(ox + SIDEBAR_WIDTH, 0,
		                      960 - (ox + SIDEBAR_WIDTH), SCREEN_HEIGHT,
		                      RGBA8(0, 0, 0, (unsigned int)(150.0f * animation)));
	vita2d_draw_rectangle(ox, 0, SIDEBAR_WIDTH, SCREEN_HEIGHT,
	                      VT_THEME_BG_SOFT);
	vita2d_draw_rectangle(ox + SIDEBAR_WIDTH - 2, 0, 2, SCREEN_HEIGHT,
	                      VT_THEME_SPECTRAL);
	vita2d_draw_rectangle(ox + SIDEBAR_WIDTH - 7, 0, 4, SCREEN_HEIGHT,
	                      VT_THEME_SIGNAL_A(42));
	vita2d_font *font = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	/* Keep decoration out of the navigation labels and moving focus band. */
	vita2d_draw_rectangle(ox + 20, 18, 42, 26, VT_THEME_SURFACE_RAISED);
	vita2d_draw_rectangle(ox + 20, 18, 3, 26, VT_THEME_SIGNAL_BRIGHT);
	if (small) {
		int key_width = ui_font_text_width(small, UI_FONT_SMALL, "L1");
		ui_font_draw_text(small, (int)ox + 41 - key_width / 2, 38,
		                  VT_THEME_TEXT, UI_FONT_SMALL, "L1");
		ui_font_draw_text(small, (int)ox + 76, 38, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, "VitaMediaDeck");
	}
	if (!font) return;
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
			vita2d_draw_rectangle(ox+14,177+i*91,396,79,VT_THEME_SURFACE);
			if(i==sidebar->audio_layout_selected)vita2d_draw_rectangle(ox+28,210+i*91,8,8,VT_THEME_SIGNAL_BRIGHT);
			ui_font_draw_text(font,(int)ox+48,222+i*91,VT_THEME_TEXT,UI_FONT_BODY,vt_i18n_str(labels[i]));
		}
		if(sidebar->open){UiFocusMotion focus={.x=ox+14,.y=177+sidebar->focus_cursor*91,.width=396,.height=79,.initialized=1};ui_focus_outline_draw(&focus);}
		if(small){
			ui_font_draw_text(small,(int)ox+20,478,VT_THEME_TEXT_MUTED,UI_FONT_SMALL,vt_i18n_str(VT_STR_MUSIC_LAYOUT_APPLIES));
			ui_font_draw_text(small,(int)ox+20,514,VT_THEME_TEXT_MUTED,UI_FONT_SMALL,vt_i18n_str(VT_STR_MUSIC_LAYOUT_UNCHANGED));
		}
		return;
	}
	int has_focus = focus_cursor >= 0.0f;
	if (has_focus) {
		vita2d_draw_rectangle(ox + ITEM_X, sidebar_row_y(sidebar, focus_cursor),
		                      ITEM_W, sidebar_row_height(), VT_THEME_SURFACE_FOCUS);
		vita2d_draw_rectangle(ox + ITEM_X, sidebar_row_y(sidebar, focus_cursor),
		                      ITEM_W, 2, VT_THEME_FOCUS);
		vita2d_draw_rectangle(ox + ITEM_X, sidebar_row_y(sidebar, focus_cursor),
		                      3, sidebar_row_height(), VT_THEME_FOCUS);
	}
	int slot_count = UI_SECTION_COUNT + sidebar->player_available +
	                 sidebar->download_available;
	for (int slot = 0; slot < slot_count; slot++) {
        int section = sidebar_section_at(sidebar, slot);
        const char *label = section == UI_SECTION_PLAYER ? vt_i18n_str(VT_STR_SECTIONS_PLAYER)
                          : section == UI_SECTION_DOWNLOAD ? vt_i18n_str(VT_STR_SECTIONS_DOWNLOAD)
                          : items[section];
		unsigned int color = has_focus && slot == cursor ? VT_THEME_TEXT
		                                      : VT_THEME_TEXT_MUTED;
		ui_font_draw_text(font, (int)ox + 34,
		                  (int)sidebar_row_y(sidebar, (float)slot) + 23, color,
		                      UI_FONT_BODY, label);
	}
    if (small && ui_theme_id() == VT_UI_THEME_LEGACY) {
        int live = sidebar->player_available + sidebar->download_available;
        const int starts[] = {0, live, live + 6};
        const VtStringId groups[] = {VT_STR_SECTIONS_GROUP_LIVE,
                                    VT_STR_SECTIONS_GROUP_CONTENT,
                                    VT_STR_SECTIONS_GROUP_APP};
        for (int group = live ? 0 : 1; group < 3; group++) {
            int y = (int)sidebar_row_y(sidebar, (float)starts[group]) - 12;
            ui_font_draw_text(small, (int)ox + 34, y, VT_THEME_SIGNAL_BRIGHT,
                              UI_FONT_SMALL, vt_i18n_str(groups[group]));
            int width = ui_font_text_width(small, UI_FONT_SMALL, vt_i18n_str(groups[group]));
            vita2d_draw_rectangle(ox + 48 + width, y - 5, ITEM_W - width - 36,
                                  1, VT_THEME_BORDER_DIM);
        }
    }
	if (small)
		ui_font_draw_text(small, (int)ox + 28, 514, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(sidebar->audio_layout_available?VT_STR_MUSIC_LAYOUT_HINT:VT_STR_SECTIONS_FOOTER_HINT));
}
