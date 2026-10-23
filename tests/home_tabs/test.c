/* Host test of the real rail and shared repeat logic. Screen-loop replacement
 * is simulated by clearing pressed while retaining held, just like entry. */
#include <assert.h>
#include <stdio.h>
#include "../../src/ui/theme.c"
#include "../../src/ui/theme_home.c"
#include "../../src/ui/theme_scene.c"
#include "../../src/ui/home_tabs.c"
static uint64_t now;
static int reduced;
int vt_preferences_reduce_motion(void){return reduced;}
uint64_t sceKernelGetProcessTimeWide(void){return now;}
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static void sample(unsigned pressed,unsigned held) {
 UiTouchEvent touch={0};
 ui_home_tabs_input(ui_home_tabs_navigation(pressed,held,128,128),0,&touch,1);
}
static void verify_order(void) {
 loaded=1;order_theme=ui_theme_id();scroll=0;sample(0,0);ui_home_tab_set(order[0]);
 for(int i=1;i<HOME_TAB_COUNT;i++) {
  now+=10000;sample(SCE_CTRL_RIGHT,SCE_CTRL_RIGHT);CHECK(active==order[i]);
  /* New local/online loop starts with held Right but no fresh edge. */
  now+=26000;sample(0,SCE_CTRL_RIGHT);CHECK(active==order[i]);
  sample(0,0);
 }
 for(int i=HOME_TAB_COUNT-2;i>=0;i--) {
  now+=10000;sample(SCE_CTRL_LEFT,SCE_CTRL_LEFT);CHECK(active==order[i]);
  now+=26000;sample(0,SCE_CTRL_LEFT);CHECK(active==order[i]);sample(0,0);
 }
 /* Continuous holding must advance once per repeat, even across a screen. */
 sample(SCE_CTRL_RIGHT,SCE_CTRL_RIGHT);CHECK(active==order[1]);
 now+=280001;sample(0,SCE_CTRL_RIGHT);CHECK(active==order[2]);
 now+=16000;sample(0,SCE_CTRL_RIGHT);CHECK(active==order[2]);sample(0,0);
}
static void verify_wheel_order(void){
 ui_theme_apply(VT_UI_THEME_GHOST_MESH);order_theme=VT_UI_THEME_GHOST_MESH;ui_home_controls_reset();
 UiHomeControlSample input={.lx=128,.ly=128,.rx=128,.ry=128,.count=8,.bottom=536,.focus_sections=1};
 static const unsigned char axes[6][2]={{128,0},{238,64},{238,192},{128,255},{18,192},{18,64}};
 now+=10000;ui_home_controls_update(&input);
 for(int i=0;i<6;i++){
  input.rx=axes[i][0];input.ry=axes[i][1];now+=10000;ui_home_controls_update(&input);CHECK(active==order[i]);
  now+=10000;UiHomeControlResult held=ui_home_controls_update(&input);CHECK(active==order[i]&&!held.sections_changed);
  input.pressed=input.held=SCE_CTRL_DOWN;input.ly=255;input.focus_sections=0;now+=10000;
  held=ui_home_controls_update(&input);CHECK(active==order[i]&&(held.content_nav&SCE_CTRL_DOWN));
  input.pressed=input.held=0;input.ly=128;now+=10000;ui_home_controls_update(&input);
 }
 ui_theme_apply(VT_UI_THEME_LEGACY);order_theme=VT_UI_THEME_LEGACY;
}
static void permutations(int p) {
 if(p==HOME_TAB_COUNT){verify_order();verify_wheel_order();return;}
 for(int i=p;i<HOME_TAB_COUNT;i++) {
  unsigned char v=order[i];order[i]=order[p];order[p]=v;
  permutations(p+1);v=order[i];order[i]=order[p];order[p]=v;
 }
}
int main(void) {
 permutations(0);
 ui_theme_apply(VT_UI_THEME_RELAY);loaded=1;order_theme=VT_UI_THEME_RELAY;ui_home_tab_set(order[0]);ui_home_controls_reset();
 UiHomeControlSample vertical={.lx=128,.ly=128,.rx=128,.ry=128,.count=9,.bottom=536,.focus_sections=1};
 vertical.pressed=vertical.held=SCE_CTRL_DOWN;now+=10000;UiHomeControlResult result=ui_home_controls_update(&vertical);
 CHECK(active==order[1]&&result.focus_sections);
 vertical.pressed=vertical.held=SCE_CTRL_RIGHT;now+=10000;result=ui_home_controls_update(&vertical);CHECK(!result.focus_sections);
 vertical.focus_sections=0;vertical.selected=3;vertical.pressed=vertical.held=SCE_CTRL_LEFT;now+=10000;
 result=ui_home_controls_update(&vertical);CHECK(!result.focus_sections&&(result.content_nav&SCE_CTRL_LEFT));
 vertical.selected=2;now+=300000;result=ui_home_controls_update(&vertical);CHECK(result.focus_sections&&!result.content_nav);
 ui_theme_apply(VT_UI_THEME_MOSAIC);loaded=1;order_theme=VT_UI_THEME_MOSAIC;ui_home_tab_set(order[0]);ui_home_controls_reset();
 vertical.focus_sections=1;vertical.pressed=vertical.held=SCE_CTRL_RIGHT;now+=10000;
 result=ui_home_controls_update(&vertical);CHECK(active==order[1]&&result.focus_sections);
 vertical.pressed=vertical.held=SCE_CTRL_DOWN;now+=10000;
 result=ui_home_controls_update(&vertical);CHECK(!result.focus_sections);
 vertical.focus_sections=0;vertical.selected=2;vertical.pressed=vertical.held=SCE_CTRL_RIGHT;now+=10000;
 result=ui_home_controls_update(&vertical);CHECK(!result.focus_sections&&(result.content_nav&SCE_CTRL_RIGHT));
 vertical.pressed=vertical.held=SCE_CTRL_UP;now+=10000;result=ui_home_controls_update(&vertical);CHECK(result.focus_sections&&!result.content_nav);
 ui_theme_apply(VT_UI_THEME_LEGACY);loaded=1;order_theme=VT_UI_THEME_LEGACY;ui_home_controls_reset();
 loaded=1;scroll_ready=0;ui_home_tab_set(HOME_LIBRARY);carousel_tick();
 CHECK(fabsf(UI_HOME_TABS_X-scroll+TAB_W*.5f-(UI_HOME_TABS_X+RAIL_W*.5f))<.1f);
 UiTouchEvent swipe={.down_x=600,.x=100,.down_y=25,.y=25};
 CHECK(ui_home_tabs_input(0,UI_TOUCH_EVENT_UP,&swipe,1));CHECK(active==HOME_VIDEO);
 float before=scroll;now+=16667;carousel_tick();
 CHECK(scroll>before && scroll<scroll_target());
 for(int i=0;i<60;i++){before=scroll;now+=16667;carousel_tick();CHECK(scroll>=before && scroll<=scroll_target());}
 CHECK(scroll==scroll_target());
 /* A tap hits the moving card, not a future target or an empty margin. */
 UiTouchEvent tap={.x=600,.y=25,.down_x=600,.down_y=25};
 CHECK(ui_home_tabs_input(0,UI_TOUCH_EVENT_TAP|UI_TOUCH_EVENT_UP,&tap,1));CHECK(active==HOME_AUDIO);
 reduced=1;carousel_tick();CHECK(scroll==scroll_target());
 tap.x=tap.down_x=497; /* 10px gap between the middle and right cards. */
 CHECK(!ui_home_tabs_input(0,UI_TOUCH_EVENT_TAP,&tap,1));
 tap.x=tap.down_x=30;CHECK(!ui_home_tabs_touch_hit(&tap));
 tap.x=tap.down_x=200;tap.y=tap.down_y=85;CHECK(!ui_home_tabs_touch_hit(&tap));
 swipe.x=590;CHECK(!ui_home_tabs_input(0,UI_TOUCH_EVENT_UP,&swipe,1));
 swipe.x=600;swipe.y=100;CHECK(!ui_home_tabs_input(0,UI_TOUCH_EVENT_UP,&swipe,1));
 ui_home_tab_set(order[0]);carousel_tick();
 tap=(UiTouchEvent){.x=100,.down_x=100,.y=25,.down_y=25};
 CHECK(!ui_home_tabs_input(0,UI_TOUCH_EVENT_TAP,&tap,1));
 ui_home_tab_set(HOME_PODCAST);ui_home_podcast_set_offline(1);
 CHECK(!ui_home_tab_online());carousel_tick();
 tap.x=tap.down_x=UI_HOME_TABS_X+RAIL_W/2;
 CHECK(ui_home_tabs_input(0,UI_TOUCH_EVENT_TAP,&tap,1));CHECK(ui_home_tab_online());
 printf("PASS: %u tab checks across all 720 orders, carousel, gestures and reduced motion\n",checks);
 return 0;
}

void vita2d_draw_array(SceGxmPrimitiveType mode,const vita2d_color_vertex*v,size_t n){(void)mode;(void)v;(void)n;}
