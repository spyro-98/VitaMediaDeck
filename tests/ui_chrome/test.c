/* Real brand/status + native Home and L1; platform readings are test inputs. */
#define VMD_ONLINE_UI
#define main iptv_ui_main
#define ui_brand_draw_header host_brand_draw_header
#define ui_brand_draw_breadcrumbs host_brand_draw_breadcrumbs
#define ui_brand_draw_home_header host_brand_draw_home_header
#define ui_brand_draw_home_search host_brand_draw_home_search
#define ui_brand_home_search_hit host_brand_home_search_hit
#define ui_brand_sections_hit host_brand_sections_hit
#include "../iptv/ui_test.c"
#undef main
#undef ui_brand_draw_header
#undef ui_brand_draw_breadcrumbs
#undef ui_brand_draw_home_header
#undef ui_brand_draw_home_search
#undef ui_brand_home_search_hit
#undef ui_brand_sections_hit
#undef VITAMEDIADECK_UI_BRAND_H
#include "ui/brand.h"
void vita2d_draw_fill_circle(float,float,float,unsigned);
#include "../../src/ui/brand.c"
#include "../../src/ui/home_tabs.c"
#include <sys/stat.h>
#define link host_posix_link
#include <unistd.h>
#undef link
static int clock12,charging,battery=73,wifi=84,status_reads,rename_failure;
int sceIoGetstat(const char *path,SceIoStat *out){struct stat st;if(stat(path,&st))return -1;out->st_size=st.st_size;return 0;}
int sceIoRemove(const char *path){return unlink(path);}
int sceIoRename(const char *from,const char *to){if(rename_failure&&strstr(from,".tmp"))return -1;return rename(from,to);}
int scePowerGetBatteryLifePercent(void){status_reads++;return battery;}
int scePowerIsBatteryCharging(void){return charging;}
int sceRegMgrSystemParamGetInt(int key,int *out){(void)key;*out=clock12;return 0;}
int sceRtcGetCurrentClockLocalTime(SceDateTime *out){memset(out,0,sizeof(*out));out->hour=23;out->minute=59;return 0;}
int vita_https_wifi_signal_percent(int *out){if(wifi<0)return -1;*out=wifi;return 0;}
void vt_display_keep_awake_tick(void){}
vita2d_texture *ui_runtime_logo(void){return NULL;}
void vt_download_background_status(VtDownloadBackgroundStatus *out){memset(out,0,sizeof(*out));out->total=100;out->current=37;}
void vita2d_draw_fill_circle(float cx,float cy,float radius,unsigned color){for(int y=(int)(cy-radius);y<=cy+radius;y++)for(int x=(int)(cx-radius);x<=cx+radius;x++)if((x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius)pixel(x,y,color,255);}
static void screen(int theme,int menu){
 vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
 ui_brand_set_loading(1);ui_brand_draw_home_header();
 ui_home_tab_set(HOME_RADIO);UiFocusMotion focus={0};ui_home_tabs_set_expanded(1);ui_home_tabs_draw_with_focus(1,&focus);
 if(theme){
  ui_theme_home_heading(language?"Radio":"Radio",vt_i18n_str(VT_STR_ONLINE_FAVORITES),536);
  UiThemeHomeLayout layout=ui_theme_home_layout(theme,536);
  for(int i=0;i<layout.rows;i++)ui_theme_home_row_art(i,i,language?"Radio salvata":"Saved station",language?"Programma / emittente":"Programme / station",i==0,0,536,vt_artwork_thumbnail_get("fixture.png"),"FM");
 }
 if(menu){UiSectionsSidebar side;ui_sections_sidebar_init(&side,UI_SECTION_ONLINE_AUDIO);side.open=1;side.animation=1;ui_sections_sidebar_draw(&side);}
}
#ifndef VMD_CHROME_HELPERS_ONLY
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];
 char storage[512];snprintf(storage,sizeof(storage),"%s/storage.XXXXXX",output);CHECK(mkdtemp(storage));CHECK(!chdir(storage));
 CHECK(!mkdir("ux0:data",0700));CHECK(!mkdir("ux0:data/VitaMediaDeck",0700));
 CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 char mono_path[1024];snprintf(mono_path,sizeof(mono_path),"%.*sIBMPlexMono-Regular.ttf",(int)(strrchr(argv[1],'/')-argv[1]+1),argv[1]);
 FT_Face inter=face,mono;CHECK(!FT_New_Face(ft,mono_path,0,&mono));
 /* Check every real clock value with each theme's actual face, including
  * both loaders. Drawing uses the exact measured face even with native PAF. */
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;
  for(int twelve=0;twelve<2;twelve++)for(int hour=0;hour<24;hour++)for(int minute=0;minute<60;minute++){
   header_status clock={.clock_valid=1,.clock_hour=hour,.clock_minute=minute,.time_format_12h=twelve};
   int left=draw_clock(&clock,1);
   CHECK(host_exact_x==left&&host_exact_x+host_exact_width<=BATT_X-22);
   int activities_left=left-STATUS_GAP-WIFI_W-STATUS_GAP-8-30-9;
   CHECK(activities_left>=SEARCH_BOX_X+SEARCH_BOX_W+12);
  }
 }
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(int mode=0;mode<3;mode++){
  face=theme==VT_UI_THEME_DECK_90?mono:inter;
  ui_theme_apply(theme);frame=0;clock12=mode==1;charging=mode==1;battery=mode==2?-1:73;wifi=mode==2?-1:84;live_slots=mode==1;download_slots=mode==1;
  memset(&s_status,0,sizeof(s_status));int reads=status_reads;screen(theme,0);CHECK(status_reads==reads+1);
  ui_brand_draw_status_indicators();CHECK(status_reads==reads+1); /* 2-second hardware cache, not 60 Hz. */
  if(language&&mode==1){char name[80];snprintf(name,sizeof(name),"theme-chrome-%d",theme);png(name);screen(theme,1);snprintf(name,sizeof(name),"theme-command-%d",theme);png(name);}
  CHECK(ui_brand_sections_hit(UI_TOUCH_EVENT_TAP,25,25));
  CHECK(ui_brand_sections_hit(UI_TOUCH_EVENT_TAP,70,1));
  CHECK(!ui_brand_sections_hit(UI_TOUCH_EVENT_TAP,76,25)); /* Tab rail begins here. */
  CHECK(!ui_brand_sections_hit(UI_TOUCH_EVENT_TAP,700,25));
 }
 /* Before network init / while offline, preserve four muted bars. Activity
  * stays to their left, with every clock width and theme already checked. */
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);vita2d_clear_screen();
  header_status unavailable={0};
  int x=draw_wifi(&unavailable,CLOCK_RIGHT-CLOCK_MAX_W,1);
  for(int i=0;i<WIFI_BARS;i++){
   int bx=x+i*(WIFI_BAR_W+WIFI_BAR_GAP);
   for(int c=0;c<3;c++)CHECK(pixels[WIFI_BOTTOM_Y-1][bx][c]==((COLOR_DIM>>(8*c))&255));
  }
  CHECK(x-STATUS_GAP-8+9<x);
 }
 ui_theme_apply(VT_UI_THEME_GHOST_MESH);loaded=0;load();unsigned char before[6];memcpy(before,order,6);
 script_count=4;memset(input_script,0,sizeof(input_script));input_script[1]=SCE_CTRL_RIGHT;input_script[3]=SCE_CTRL_CIRCLE;frame=0;
 CHECK(ui_home_tabs_configure()==0&&!memcmp(order,before,6));
 frame=0;capture_frame=2;input_script[3]=SCE_CTRL_CROSS;
 CHECK(ui_home_tabs_configure()==0&&order[0]==before[1]&&order[1]==before[0]);
 unsigned char committed[6];memcpy(committed,order,6);loaded=0;load();CHECK(!memcmp(committed,order,6));
 memcpy(pixels,first_pixels,sizeof(pixels));png("theme-positions-1");
 rename_failure=1;frame=0;CHECK(ui_home_tabs_configure()<0&&!memcmp(committed,order,6));rename_failure=0;
 for(int theme=1;theme<VT_UI_THEME_COUNT;theme++) {
  ui_theme_apply(theme);frame=0;script_count=2;input_script[0]=0;input_script[1]=SCE_CTRL_CIRCLE;capture_frame=0;
  ui_home_tabs_configure();memcpy(pixels,first_pixels,sizeof(pixels));char name[80];snprintf(name,sizeof(name),"theme-positions-%d",theme);png(name);
 }
 /* R1 drag reaches actions beyond the original eight-row viewport. */
 UiContextMenu menu={.open=1,.animation=1};UiContextAction actions[12];
 for(int i=0;i<12;i++)actions[i]=(UiContextAction){"Action",NULL,100+i,1};
 unsigned press=0,flags=UI_TOUCH_EVENT_DOWN;UiTouchEvent drag={.x=600,.y=470,.down_x=600,.down_y=470};
 CHECK(ui_context_menu_input(&menu,actions,12,&press,0,128,&flags,&drag)<0);
 drag.y=10;flags=UI_TOUCH_EVENT_MOVE;CHECK(ui_context_menu_input(&menu,actions,12,&press,0,128,&flags,&drag)<0);
 CHECK(menu.cursor==4 && menu.top==4 && menu.viewport.position==208);
 drag.x=600;drag.y=94+7*52+20;flags=UI_TOUCH_EVENT_TAP;
 CHECK(ui_context_menu_input(&menu,actions,12,&press,0,128,&flags,&drag)==111);
 CHECK(!menu.open);
 /* Category headings are genuine gaps, never hit targets or selected rows. */
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;
  CHECK(UI_MENU_HEADING_HEIGHT==34);
  host_record_text=1;host_text_count=0;ui_menu_category(20,80,300,UI_MENU_GROUP_VIEW);
  CHECK(host_text_count==1&&host_text_rects[0].y==105);
  CHECK(host_text_rects[0].y-16-80==9); /* +5 px above the heading versus the prior 4. */
  CHECK(80+UI_MENU_HEADING_HEIGHT-host_text_rects[0].y==9); /* -3 px below versus 12. */
  host_record_text=0;
  UiContextAction grouped[]={
   {"Fill screen","Off",1,(1|UI_MENU_GROUP_PLAYBACK<<8)},
   {"Reconnect live",NULL,2,(1|UI_MENU_GROUP_PLAYBACK<<8)},
   {"Video quality","Automatic",3,(1|UI_MENU_GROUP_VIEW<<8)},
   {"Audio language","Source default",4,(1|UI_MENU_GROUP_VIEW<<8)},
   {"Programme",NULL,5,(1|UI_MENU_GROUP_INFO<<8)}};
  UiContextMenu groups={.open=1,.animation=1};unsigned p=0,t=0;UiTouchEvent tap={0};
  CHECK(ui_context_menu_input(&groups,grouped,5,&p,0,128,&t,&tap)<0);
  CHECK(groups.group_starts==((1u<<0)|(1u<<2)|(1u<<4)));
  float before=ui_menu_group_y(1,94,52,groups.group_starts),after=ui_menu_group_y(2,94,52,groups.group_starts);
  CHECK(after-before==52+UI_MENU_HEADING_HEIGHT);
  CHECK(ui_menu_group_focus_y(1.5f,94,52,groups.group_starts)==(before+after)*.5f);
  tap.x=600;tap.y=after-UI_MENU_HEADING_HEIGHT/2;t=UI_TOUCH_EVENT_TAP;
  CHECK(ui_context_menu_input(&groups,grouped,5,&p,0,128,&t,&tap)<0&&groups.cursor==0&&groups.open);
  tap.y=after+20;t=UI_TOUCH_EVENT_TAP;
  CHECK(ui_context_menu_input(&groups,grouped,5,&p,0,128,&t,&tap)==3&&groups.cursor==2);
  groups.open=1;groups.animation=1;groups.focus.initialized=0;ui_context_menu_tick(&groups);
  vita2d_clear_screen();ui_context_menu_draw(&groups,"Playback",grouped,5);
  char name[80];snprintf(name,sizeof(name),"r1-heading-slots-%d-%u",theme,language);png(name);
 }
 FT_Done_Face(mono);FT_Done_Face(inter);
 FT_Done_FreeType(ft);
 printf("Production theme chrome: %u checks, 8 themes, IT/EN, 12h/24h, live activities, unknown "
        "status and command decks passed.\n",
        checks);
 return 0;
}

#endif
