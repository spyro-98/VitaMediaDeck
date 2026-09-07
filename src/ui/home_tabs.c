#include "ui/home_tabs.h"
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <vita2d.h>
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "i18n/i18n.h"
static int active, loaded, scroll;
static unsigned char order[HOME_TAB_COUNT] = {0,1,2,3,4,5,6};
#define ORDER_PATH "ux0:data/VitaMediaDeck/home-tabs.bin"
#define TAB_W 166
#define TAB_STEP 172
#define RAIL_W 876
static const char *label(int id) {
 static const VtStringId keys[]={VT_STR_HOME_LIBRARY,VT_STR_LOCAL_MEDIA_LOCAL_VIDEO,
 VT_STR_LOCAL_MEDIA_LOCAL_AUDIO,VT_STR_ONLINE_RADIO,VT_STR_HOME_PODCAST_ONLINE,
 VT_STR_HOME_PODCAST_OFFLINE,VT_STR_LOCAL_MEDIA_LOCAL_IMAGES};
 return vt_i18n_str(keys[id]);
}
static int valid(const unsigned char *v) {
 unsigned mask=0;
 for(int i=0;i<HOME_TAB_COUNT;i++){
  if(v[i]>=HOME_TAB_COUNT||(mask&(1u<<v[i])))return 0;
  mask|=1u<<v[i];
 }return 1;
}
static void load(void){
 if(loaded)return;
 loaded=1;unsigned char value[HOME_TAB_COUNT];FILE *f=fopen(ORDER_PATH,"rb");
 if(!f)f=fopen(ORDER_PATH ".bak","rb");
 if(f){if(fread(value,1,sizeof(value),f)==sizeof(value)&&valid(value))memcpy(order,value,sizeof(order));fclose(f);}
}
static int position(void){load();for(int i=0;i<HOME_TAB_COUNT;i++)if(order[i]==active)return i;return 0;}
static void reveal(void){
 int x=position()*TAB_STEP;
 if(x<scroll)scroll=x;
 if(x+TAB_W>scroll+RAIL_W)scroll=x+TAB_W-RAIL_W;
}
int ui_home_tab(void){load();return active;}
void ui_home_tab_set(int id){load();if(id>=0&&id<HOME_TAB_COUNT)active=id;reveal();}
int ui_home_tab_online(void){return active==HOME_RADIO||active==HOME_PODCAST_ONLINE;}
int ui_home_tab_filter(void){static const int filters[]={0,1,2,0,0,4,3};return filters[ui_home_tab()];}
unsigned ui_home_tabs_navigation(unsigned pressed,unsigned held,unsigned char lx,unsigned char ly) {
 /* One repeat clock for the rail, including transitions between screen loops. */
 static UiNavRepeat repeat;
 return ui_nav_repeat_update(&repeat,pressed,held,lx,ly,SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
}
int ui_home_tabs_input(unsigned nav,unsigned flags,const UiTouchEvent *t,int focused){
 int before=active;load();
 if(focused&&(nav&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))){
  int p=position()+(nav&SCE_CTRL_LEFT?-1:1);
  if(p>=0&&p<HOME_TAB_COUNT){active=order[p];reveal();}
 }
 if((flags&UI_TOUCH_EVENT_TAP)&&t->y>=68&&t->y<108&&t->x>=42&&t->x<918){
  int p=(t->x-42+scroll)/TAB_STEP;
  if(p<HOME_TAB_COUNT){active=order[p];reveal();}
 }else if((flags&UI_TOUCH_EVENT_UP)&&!(flags&UI_TOUCH_EVENT_TAP)&&t->down_y>=68&&t->down_y<108){
  scroll-=t->x-t->down_x;
  if(scroll<0)scroll=0;
  if(scroll>HOME_TAB_COUNT*TAB_STEP-6-RAIL_W)scroll=HOME_TAB_COUNT*TAB_STEP-6-RAIL_W;
 }
 return active!=before;
}
void ui_home_tabs_draw(int focused){
 load();vita2d_font *f=ui_runtime_font(UI_FONT_SMALL);
 vita2d_enable_clipping();vita2d_set_clip_rectangle(42,66,918,110);
 for(int i=0;i<HOME_TAB_COUNT;i++){
  int x=42+i*TAB_STEP-scroll;
  ui_panel(x,68,TAB_W,38,order[i]==active?VT_THEME_SURFACE_FOCUS:VT_THEME_SURFACE,
   VT_THEME_SIGNAL_LIGHT,focused&&order[i]==active);
  char text[128];ui_font_fit_text(f,UI_FONT_SMALL,label(order[i]),text,sizeof(text),TAB_W-16);
  if(f)ui_font_draw_text(f,x+8,94,order[i]==active?VT_THEME_TEXT:VT_THEME_TEXT_MUTED,UI_FONT_SMALL,text);
 }vita2d_disable_clipping();
}
int ui_home_tabs_configure(void){
 load();unsigned char backup[HOME_TAB_COUNT];memcpy(backup,order,sizeof(order));
 SceCtrlData old={0},c;sceCtrlPeekBufferPositive(0,&old,1);int selected=0,result=0;ui_touch_reset();
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
   FILE *f=fopen(ORDER_PATH ".tmp","wb");result=-1;
   if(f){int ok=fwrite(order,1,sizeof(order),f)==sizeof(order);if(fflush(f))ok=0;if(fclose(f))ok=0;
    if(ok){SceIoStat st;int exists=sceIoGetstat(ORDER_PATH,&st)>=0;
     if(exists){sceIoRemove(ORDER_PATH ".bak");if(sceIoRename(ORDER_PATH,ORDER_PATH ".bak")<0)ok=0;}
     if(ok&&sceIoRename(ORDER_PATH ".tmp",ORDER_PATH)>=0){result=0;sceIoRemove(ORDER_PATH ".bak");}
     else if(exists&&ok)sceIoRename(ORDER_PATH ".bak",ORDER_PATH);
    }}
   if(result<0)memcpy(order,backup,sizeof(order));
   break;
  }
  vita2d_start_drawing();vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD_LIGHT);ui_brand_draw_header(NULL);
  vita2d_font *f=ui_runtime_font(UI_FONT_SMALL);
  if(f)ui_font_draw_text(f,66,96,VT_THEME_TEXT,UI_FONT_SMALL,vt_i18n_str(VT_STR_HOME_REORDER_HINT));
  for(int i=0;i<HOME_TAB_COUNT;i++){
   ui_panel(66,120+i*46,828,42,VT_THEME_SURFACE,VT_THEME_SIGNAL_LIGHT,i==selected);
   if(f){char row[160];snprintf(row,sizeof(row),"<     %d. %s     >",i+1,label(order[i]));ui_font_draw_text(f,86,148+i*46,VT_THEME_TEXT,UI_FONT_SMALL,row);}
  }
  ui_action_button(66,454,390,44,VT_THEME_SURFACE,"X",vt_i18n_str(VT_STR_ONLINE_SAVE),0);
  ui_action_button(504,454,390,44,VT_THEME_SURFACE,"Circle","Back",0);
  vita2d_end_drawing();vita2d_wait_rendering_done();vita2d_swap_buffers();sceKernelDelayThread(16000);
 }reveal();ui_touch_reset();return result;
}
