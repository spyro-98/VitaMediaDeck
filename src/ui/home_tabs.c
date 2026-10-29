#include "ui/home_tabs.h"
#include "ui/scroll_view.h"
#include <stdio.h>
#include <math.h>
#include "settings/preferences.h"
#include <string.h>
#include <stdlib.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <vita2d.h>
#include "ui/brand.h"
#include "ui/layout.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/theme_home.h"
#include "i18n/i18n.h"
static int active, loaded;
static int order_theme=-1;
static int expanded_home;
void ui_home_tabs_set_expanded(int expanded){expanded_home=!!expanded;}
static float scroll;
static UiScrollView rail;
static int rail_free;
static uint64_t scroll_tick_us;
static int scroll_ready;
static int podcast_offline;
static unsigned char order[HOME_TAB_COUNT] = {0,1,2,3,4,5};
#define ORDER_PATH "ux0:data/VitaMediaDeck/home-tabs.bin"
#define TAB_W 200
#define TAB_STEP 210
#define RAIL_W (UI_HOME_TABS_RIGHT - UI_HOME_TABS_X)
static const char *label(int id) {
 static const VtStringId keys[]={VT_STR_HOME_LIBRARY,VT_STR_LOCAL_MEDIA_LOCAL_VIDEO,
 VT_STR_LOCAL_MEDIA_LOCAL_AUDIO,VT_STR_ONLINE_RADIO,VT_STR_HOME_PODCAST,
 VT_STR_LOCAL_MEDIA_LOCAL_IMAGES};
 return vt_i18n_str(keys[id]);
}
static int valid(const unsigned char *v) {
 unsigned mask=0;
 for(int i=0;i<HOME_TAB_COUNT;i++){
  if(v[i]>=HOME_TAB_COUNT||(mask&(1u<<v[i])))return 0;
  mask|=1u<<v[i];
 }return 1;
}
static void order_path(int theme,char path[128],const char *suffix) {
 snprintf(path,128,"ux0:data/VitaMediaDeck/home-tabs-%d.bin%s",theme,suffix?suffix:"");
}
static int order_read(const char *path,unsigned char value[HOME_TAB_COUNT]) {
 unsigned char bytes[8];FILE *f=fopen(path,"rb");if(!f)return -1;
 size_t count=fread(bytes,1,sizeof(bytes),f);fclose(f);
 if(count==HOME_TAB_COUNT&&valid(bytes)){memcpy(value,bytes,HOME_TAB_COUNT);return 0;}
 if(count==7){unsigned mask=0;int ok=1;
  for(int i=0;i<7;i++){if(bytes[i]>6||(mask&(1u<<bytes[i])))ok=0;else mask|=1u<<bytes[i];}
  if(ok){int j=0;for(int i=0;i<7;i++)if(bytes[i]!=5)value[j++]=bytes[i]==6?HOME_IMAGES:bytes[i];return 0;}
 }
 return -1;
}
static void load(void){
 int theme=ui_theme_id();
 if(loaded&&order_theme==theme)return;
 order_theme=theme;loaded=1;scroll_ready=0;rail_free=0;ui_scroll_reset(&rail,0);
 for(int i=0;i<HOME_TAB_COUNT;i++)order[i]=(unsigned char)i;
 unsigned char value[HOME_TAB_COUNT];char path[128];order_path(theme,path,"");
 int result=order_read(path,value);
 if(result<0){order_path(theme,path,".bak");result=order_read(path,value);}
 /* Seed every new theme from the user's previous global order. No write is
  * needed until Save; independently saved theme orders never leak across. */
 if(result<0)result=order_read(ORDER_PATH,value);
 if(result<0)result=order_read(ORDER_PATH ".bak",value);
 if(result==0)memcpy(order,value,sizeof(order));
}
static int save_order(void) {
 char path[128],temp[128],backup[128];order_path(ui_theme_id(),path,"");
 order_path(ui_theme_id(),temp,".tmp");order_path(ui_theme_id(),backup,".bak");
 FILE *f=fopen(temp,"wb");if(!f)return -1;
 int ok=fwrite(order,1,sizeof(order),f)==sizeof(order);if(fflush(f))ok=0;if(fclose(f))ok=0;
 SceIoStat st;int existed=sceIoGetstat(path,&st)>=0;
 unsigned char previous[HOME_TAB_COUNT];
 /* A recovered backup must not be replaced with a corrupt primary on Save. */
 if(ok&&existed&&order_read(path,previous)<0){sceIoRemove(path);existed=0;}
 if(ok&&existed){sceIoRemove(backup);if(sceIoRename(path,backup)<0)ok=0;}
 if(ok&&sceIoRename(temp,path)>=0){sceIoRemove(backup);return 0;}
 if(existed)sceIoRename(backup,path);
 sceIoRemove(temp);return -1;
}
static int position(void){load();for(int i=0;i<HOME_TAB_COUNT;i++)if(order[i]==active)return i;return 0;}
static float scroll_target(void) {
 return position() * TAB_STEP - (RAIL_W - TAB_W) * .5f;
}
static void reveal(void) {
 /* Preserve the visual position across screen changes; the next draw animates
  * towards the newly selected tab rather than snapping the whole rail. */
 if (!scroll_ready) { scroll = scroll_target(); scroll_ready = 1;ui_scroll_reset(&rail,scroll+(RAIL_W-TAB_W)*.5f); }
}
static void carousel_tick(void) {
 reveal();
 uint64_t now = sceKernelGetProcessTimeWide();
 float seconds = scroll_tick_us && now > scroll_tick_us
               ? (float)(now - scroll_tick_us) / 1000000.0f : 1.0f / 60.0f;
 scroll_tick_us = now;
 if (seconds > .05f) seconds = .05f;
 if(rail_free){
  ui_scroll_bounds(&rail,(HOME_TAB_COUNT-1)*TAB_STEP+RAIL_W,RAIL_W);
  ui_scroll_tick(&rail,now,vt_preferences_reduce_motion());
  scroll=rail.position-(RAIL_W-TAB_W)*.5f;return;
 }
 float target = scroll_target();
 if (vt_preferences_reduce_motion()) scroll = target;
 else scroll += (target - scroll) * (1.0f - expf(-20.0f * seconds));
 if (fabsf(target - scroll) < .1f) scroll = target;
}
int ui_home_tabs_touch_hit(const UiTouchEvent *t) {
    if (t && !vt_ui_theme_legacy_layout(ui_theme_id())) {
        for (int i = 0; i < HOME_TAB_COUNT; i++) {
            UiThemeRect r = ui_theme_home_tab_rect(ui_theme_id(), i, expanded_home);
            if (t->down_x >= r.x && t->down_x < r.x + r.w && t->down_y >= r.y &&
                t->down_y < r.y + r.h)
                return 1;
        }
        return 0;
    }
 return t && t->down_x >= UI_HOME_TABS_X && t->down_x < UI_HOME_TABS_RIGHT &&
        t->down_y >= UI_HOME_TABS_Y && t->down_y < UI_HOME_TABS_BOTTOM;
}
int ui_home_tab(void){load();return active;}
const char *ui_home_tab_label(void){return label(ui_home_tab());}
void ui_home_tab_set(int id){load();if(id>=0&&id<HOME_TAB_COUNT){active=id;podcast_offline=0;rail_free=0;}reveal();}
void ui_home_podcast_set_offline(int offline){podcast_offline=!!offline;}
int ui_home_tab_online(void){return active==HOME_RADIO||(active==HOME_PODCAST&&!podcast_offline);}
int ui_home_tab_filter(void){static const int filters[]={0,1,2,0,4,3};return filters[ui_home_tab()];}
unsigned ui_home_tabs_navigation(unsigned pressed,unsigned held,unsigned char lx,unsigned char ly) {
 /* One repeat clock for the rail, including transitions between screen loops. */
 static UiNavRepeat repeat;
 return ui_nav_repeat_update(&repeat,pressed,held,lx,ly,SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
}
int ui_home_tabs_input(unsigned nav,unsigned flags,const UiTouchEvent *t,int focused){
 int before=active,before_offline=podcast_offline;load();reveal();
 if(focused&&(nav&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))){
  rail_free=0;
  int p=position()+(nav&SCE_CTRL_LEFT?-1:1);
  if(p>=0&&p<HOME_TAB_COUNT){active=order[p];podcast_offline=0;reveal();}
 }
 if (!vt_ui_theme_legacy_layout(ui_theme_id())) {
     if ((flags & UI_TOUCH_EVENT_TAP) && ui_home_tabs_touch_hit(t))
         for (int i = 0; i < HOME_TAB_COUNT; i++) {
             UiThemeRect r = ui_theme_home_tab_rect(ui_theme_id(), i, expanded_home);
             if (t->x >= r.x && t->x < r.x + r.w && t->y >= r.y && t->y < r.y + r.h) {
                 active = order[i];
                 podcast_offline = 0;
             }
         }
     return active != before || podcast_offline != before_offline;
 }
 if((flags&UI_TOUCH_EVENT_TAP) && ui_home_tabs_touch_hit(t) &&
    t->y>=UI_HOME_TABS_Y && t->y<UI_HOME_TABS_BOTTOM &&
    t->x>=UI_HOME_TABS_X && t->x<UI_HOME_TABS_RIGHT) {
  /* Hit the actual animated cards, including gaps and the blank end margins. */
  float rail_x=t->x-UI_HOME_TABS_X+scroll;
  int p=(int)floorf(rail_x/TAB_STEP);
  if(p>=0 && p<HOME_TAB_COUNT && rail_x-p*TAB_STEP<TAB_W) {
   active=order[p];podcast_offline=0;rail_free=0;reveal();
  }
 }
 if(t&&ui_home_tabs_touch_hit(t)){
  uint64_t now=sceKernelGetProcessTimeWide();
  float dt=rail.last&&now>rail.last?(now-rail.last)/1000000.f:1.f/60;
  if(dt>.05f)dt=.05f;
  if(flags&UI_TOUCH_EVENT_DOWN){
   ui_scroll_reset(&rail,scroll+(RAIL_W-TAB_W)*.5f);rail.ready=1;rail.last=now;
   ui_scroll_bounds(&rail,(HOME_TAB_COUNT-1)*TAB_STEP+RAIL_W,RAIL_W);
   ui_scroll_drag_begin(&rail,t->x);rail_free=1;
  }
  if(rail.dragging&&(flags&(UI_TOUCH_EVENT_MOVE|UI_TOUCH_EVENT_HOLD|UI_TOUCH_EVENT_UP))){
   ui_scroll_drag_move(&rail,t->down_x,t->x,dt);rail.last=now;
   if(flags&UI_TOUCH_EVENT_UP)ui_scroll_drag_end(&rail,vt_preferences_reduce_motion());
   scroll=rail.position-(RAIL_W-TAB_W)*.5f;
  }
 }

 return active!=before||podcast_offline!=before_offline;
}
void ui_home_tabs_draw(int focused){
 static UiFocusMotion motion;
 ui_home_tabs_draw_with_focus(focused,&motion);
}
void ui_home_tabs_draw_with_focus(int focused,UiFocusMotion *motion){
 load();
 if (!vt_ui_theme_legacy_layout(ui_theme_id())) {
     {
         const char *labels[HOME_TAB_COUNT];
         for (int i = 0; i < HOME_TAB_COUNT; i++)
             labels[i] = label(order[i]);
         ui_theme_home_menu(ui_theme_id(), position(), labels, order, focused, motion);
     }
     return;
 }
 carousel_tick();vita2d_font *f=ui_runtime_font(UI_FONT_SMALL);
 vita2d_set_clip_rectangle(UI_HOME_TABS_X,UI_HOME_TABS_Y-2,UI_HOME_TABS_RIGHT,UI_HOME_TABS_BOTTOM+2);
 vita2d_enable_clipping();
 for(int i=0;i<HOME_TAB_COUNT;i++) {
  float x=UI_HOME_TABS_X+i*TAB_STEP-scroll;
  if(x+TAB_W<=UI_HOME_TABS_X || x>=UI_HOME_TABS_RIGHT)continue;
  int selected=order[i]==active;
  ui_menu_row(x,UI_HOME_TABS_Y,TAB_W,UI_HOME_TABS_H,0,1);
  if(selected)vita2d_draw_rectangle(x+12,UI_HOME_TABS_BOTTOM-2,TAB_W-24,2,VT_THEME_SIGNAL_BRIGHT);
  if(f) {
   char text[128];
   int width=ui_font_fit_text(f,UI_FONT_SMALL,label(order[i]),text,sizeof(text),TAB_W-24);
   ui_menu_text(x+(TAB_W-width)*.5f,UI_HOME_TABS_Y+25,width,text,
                    selected?VT_THEME_TEXT:VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  }
 }
 if(focused) {
  /* The selected tab and its focus rim travel together; no second lagging
   * animation competes with the carousel during a held direction. */
  *motion=(UiFocusMotion){.x=UI_HOME_TABS_X+position()*TAB_STEP-scroll,
      .y=UI_HOME_TABS_Y,.width=TAB_W,.height=UI_HOME_TABS_H,.initialized=1};
  ui_focus_outline_draw(motion);
 }
 vita2d_disable_clipping();
}

static void position_label(int position,char *out,size_t capacity) {
 int theme=ui_theme_id();
 static const VtStringId wheel[]={VT_STR_HOME_POSITION_TOP,VT_STR_HOME_POSITION_UPPER_RIGHT,
  VT_STR_HOME_POSITION_LOWER_RIGHT,VT_STR_HOME_POSITION_BOTTOM,VT_STR_HOME_POSITION_LOWER_LEFT,VT_STR_HOME_POSITION_UPPER_LEFT};
 if(theme==VT_UI_THEME_GHOST_MESH)snprintf(out,capacity,"%s",vt_i18n_str(wheel[position]));
 else snprintf(out,capacity,vt_i18n_str(theme==VT_UI_THEME_APERTURE?VT_STR_HOME_POSITION_LOWER_RAIL:
    (theme==VT_UI_THEME_DECK_90||theme==VT_UI_THEME_RELAY)?VT_STR_HOME_POSITION_ROW:VT_STR_HOME_POSITION_RAIL),position+1);
}
int ui_home_tabs_configure(void){
 load();unsigned char backup[HOME_TAB_COUNT];memcpy(backup,order,sizeof(order));
 SceCtrlData old={0},c;sceCtrlPeekBufferPositive(0,&old,1);int selected=0,result=0;UiFocusMotion motion={0};ui_touch_reset();
 for(;;){
  sceCtrlPeekBufferPositive(0,&c,1);unsigned p=c.buttons&~old.buttons;old=c;
  UiTouchEvent t;unsigned flags=ui_touch_poll(&t);
  if(ui_brand_sections_hit(flags,t.x,t.y))p|=SCE_CTRL_LTRIGGER;
  if(flags&UI_TOUCH_EVENT_TAP){
   if(ui_touch_hit_rect(t.x,t.y,66,454,390,44))p|=SCE_CTRL_CROSS;
   if(ui_touch_hit_rect(t.x,t.y,504,454,390,44))p|=SCE_CTRL_CIRCLE;
  }
  if(flags&UI_TOUCH_EVENT_TAP)for(int i=0;i<HOME_TAB_COUNT;i++)
   if(ui_touch_hit_rect(t.x,t.y,66,120+i*46,828,42)){selected=i;if(t.x>750)p|=SCE_CTRL_RIGHT;else if(t.x<160)p|=SCE_CTRL_LEFT;}
  if((p&SCE_CTRL_UP)&&selected>0)selected--;
  if((p&SCE_CTRL_DOWN)&&selected<HOME_TAB_COUNT-1)selected++;
  int to=selected+(p&SCE_CTRL_LEFT?-1:p&SCE_CTRL_RIGHT?1:0);
  if(to>=0&&to<HOME_TAB_COUNT&&to!=selected){unsigned char v=order[to];order[to]=order[selected];order[selected]=v;selected=to;}
  if(p&(SCE_CTRL_CIRCLE|SCE_CTRL_LTRIGGER)){memcpy(order,backup,sizeof(order));break;}
  if(p&SCE_CTRL_CROSS){
   result=save_order();
   if(result<0)memcpy(order,backup,sizeof(order));
   break;
  }
  vita2d_start_drawing();vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD_LIGHT);ui_brand_draw_header(NULL);
  vita2d_font *f=ui_runtime_font(UI_FONT_SMALL);
  if(f){
   ui_text_line(66,88,828,ui_theme_name(ui_theme_id()),VT_THEME_SIGNAL_BRIGHT,UI_FONT_BODY);
   ui_text_line(66,112,828,vt_i18n_str(VT_STR_HOME_REORDER_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  }
  for(int i=0;i<HOME_TAB_COUNT;i++){
   ui_menu_row(66,120+i*46,828,46,i==selected,1);
   if(f){char row[160],place[128];snprintf(row,sizeof(row),"%d   %s",i+1,label(order[i]));
    position_label(i,place,sizeof(place));
    ui_text_line(80,148+i*46,20,"<",VT_THEME_SIGNAL_BRIGHT,UI_FONT_SMALL);
    ui_text_line(116,148+i*46,404,row,VT_THEME_TEXT,UI_FONT_SMALL);
    ui_text_line(536,148+i*46,300,place,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
    ui_text_line(864,148+i*46,20,">",VT_THEME_SIGNAL_BRIGHT,UI_FONT_SMALL);
   }
  }
  ui_focus_motion_tick(&motion,66,120+selected*46,828,42);ui_menu_focus_draw(&motion);
  ui_action_button(66,454,390,44,VT_THEME_SURFACE,UI_PAD_CROSS,vt_i18n_str(VT_STR_ONLINE_SAVE),0);
  ui_action_button(504,454,390,44,VT_THEME_SURFACE,UI_PAD_CIRCLE,vt_i18n_str(VT_STR_NETWORK_CANCEL),0);
  vita2d_end_drawing();vita2d_wait_rendering_done();vita2d_swap_buffers();sceKernelDelayThread(16000);
 }reveal();ui_touch_reset();return result;
}

/* One controller owner survives local/online screen changes. The right stick
 * never becomes a content stick because a list acquired focus. */
static UiNavRepeat home_content_repeat;
static int controls_theme=-1,wheel_sector=-1,wheel_wait_neutral=1;
void ui_home_controls_reset(void){
 ui_nav_repeat_reset(&home_content_repeat);controls_theme=ui_theme_id();
 wheel_sector=-1;wheel_wait_neutral=1;
}
static void home_select_position(int p){
 if(p>=0&&p<HOME_TAB_COUNT)ui_home_tab_set(order[p]);
}
UiHomeControlResult ui_home_controls_update(const UiHomeControlSample *s){
 UiHomeControlResult out={.pressed=s->pressed,.focus_sections=s->focus_sections};
 int theme=ui_theme_id(),before=ui_home_tab(),before_offline=podcast_offline;
 if(theme!=controls_theme)ui_home_controls_reset();
 if(s->blocked){ui_home_controls_reset();out.pressed=0;return out;}
 unsigned nav=ui_nav_repeat_update(&home_content_repeat,s->pressed,s->held,s->lx,s->ly,
     SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
 if(vt_ui_theme_legacy_layout(theme)&&s->focus_sections) {
  unsigned rail=ui_home_tabs_navigation(s->pressed,s->held,s->lx,s->ly);
  nav=(nav&~(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))|rail;
 }
 if(theme==VT_UI_THEME_GHOST_MESH){
  int dx=(int)s->rx-128,dy=(int)s->ry-128,length=dx*dx+dy*dy;
  if(length<=40*40){wheel_sector=-1;wheel_wait_neutral=0;}
  else if(!wheel_wait_neutral&&length>=60*60){
   static const float axes[6][2]={{0,-1},{.8660254f,-.5f},{.8660254f,.5f},
                                {0,1},{-.8660254f,.5f},{-.8660254f,-.5f}};
   int target=0;float best=-1000;
   for(int i=0;i<6;i++){float dot=dx*axes[i][0]+dy*axes[i][1];if(dot>best){best=dot;target=i;}}
   if(wheel_sector>=0){float retained=dx*axes[wheel_sector][0]+dy*axes[wheel_sector][1];
    if(retained+10>=best)target=wheel_sector;}
   if(target!=wheel_sector){wheel_sector=target;home_select_position(target);out.focus_sections=1;}
  }
  /* D-pad and left stick always address the content pane, even at its edge. */
  out.content_nav=nav;
  if(nav||(s->pressed&SCE_CTRL_CROSS))out.focus_sections=0;
 }else if(s->focus_sections){
  int p=position(),target=p;
  if(theme==VT_UI_THEME_DECK_90||theme==VT_UI_THEME_RELAY){
   if(nav&SCE_CTRL_UP)target--;
   if(nav&SCE_CTRL_DOWN)target++;
   if((nav&SCE_CTRL_RIGHT)&&s->count>0)out.focus_sections=0;
  }else{
   if(nav&SCE_CTRL_LEFT)target--;
   if(nav&SCE_CTRL_RIGHT)target++;
   if(s->count>0&&(nav&(theme==VT_UI_THEME_APERTURE?SCE_CTRL_UP:SCE_CTRL_DOWN)))out.focus_sections=0;
  }
  if(target!=p)home_select_position(target);
  if(s->pressed&SCE_CTRL_CROSS){if(s->count>0)out.focus_sections=0;out.pressed&=~SCE_CTRL_CROSS;}
 }else{
  out.content_nav=nav;
  int columns=ui_theme_home_layout(theme,s->bottom).columns;
  int leave=theme==VT_UI_THEME_RELAY?((nav&SCE_CTRL_LEFT)&&s->selected%columns==0):
      theme==VT_UI_THEME_DECK_90?(nav&SCE_CTRL_LEFT):
      theme==VT_UI_THEME_APERTURE?((nav&SCE_CTRL_DOWN)&&(s->selected/columns+1)*columns>=s->count):
      ((nav&SCE_CTRL_UP)&&s->selected<columns);
  if (leave && !s->geometric_content) {
      out.focus_sections = 1;
      out.content_nav = 0;
  }
 }
 /* Entering content consumes the gesture; it must not also move a row. */
 if(s->focus_sections && !out.focus_sections) {
  out.content_nav=0;out.pressed&=~SCE_CTRL_CROSS;
 }
 if(s->touch_flags&UI_TOUCH_EVENT_TAP){
  if(ui_home_tabs_touch_hit(s->touch)){
   ui_home_tabs_input(0,s->touch_flags,s->touch,0);out.focus_sections=1;
  }else out.focus_sections=0;
 }
 out.sections_changed=before!=ui_home_tab()||before_offline!=podcast_offline;
 if(out.sections_changed){out.content_nav=0;out.pressed&=~SCE_CTRL_CROSS;}
 if(!s->count&&theme!=VT_UI_THEME_GHOST_MESH)out.focus_sections=1;
 return out;
}
