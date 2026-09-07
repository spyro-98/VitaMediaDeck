/* Host test of the real rail and shared repeat logic. Screen-loop replacement
 * is simulated by clearing pressed while retaining held, just like entry. */
#include <assert.h>
#include <stdio.h>
#include "../../src/ui/home_tabs.c"
static uint64_t now;
uint64_t sceKernelGetProcessTimeWide(void){return now;}
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static void sample(unsigned pressed,unsigned held) {
 UiTouchEvent touch={0};
 ui_home_tabs_input(ui_home_tabs_navigation(pressed,held,128,128),0,&touch,1);
}
static void verify_order(void) {
 loaded=1;scroll=0;sample(0,0);ui_home_tab_set(order[0]);
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
static void permutations(int p) {
 if(p==HOME_TAB_COUNT){verify_order();return;}
 for(int i=p;i<HOME_TAB_COUNT;i++) {
  unsigned char v=order[i];order[i]=order[p];order[p]=v;
  permutations(p+1);v=order[i];order[i]=order[p];order[p]=v;
 }
}
int main(void) {
 permutations(0);
 loaded=1;scroll=0;ui_home_tab_set(HOME_LIBRARY);
 UiTouchEvent swipe={.down_x=800,.x=100,.down_y=85,.y=85};
 ui_home_tabs_input(0,UI_TOUCH_EVENT_UP,&swipe,1);CHECK(active==HOME_LIBRARY);
 CHECK(scroll==HOME_TAB_COUNT*TAB_STEP-6-RAIL_W);
 UiTouchEvent tap={.x=918-10,.y=85};
 ui_home_tabs_input(0,UI_TOUCH_EVENT_TAP|UI_TOUCH_EVENT_UP,&tap,1);CHECK(active==HOME_IMAGES);
 printf("PASS: %u tab checks across all 5040 orders and both directions\n",checks);
}
