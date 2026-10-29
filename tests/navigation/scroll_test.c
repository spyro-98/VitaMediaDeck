#include "ui/page_motion.h"
#include <stdio.h>
#include <assert.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
int main(void){
 for(int theme=0;theme<8;theme++)for(int reduced=0;reduced<2;reduced++)for(int columns=1;columns<=3;columns++){
  UiPageMotion m={0};int first=0,selected=0;uint64_t now=1000000;
  int count=83,slots=columns*3;float unit=73;UiTouchEvent t={.down_x=100,.down_y=300,.x=100,.y=300};unsigned flags=UI_TOUCH_EVENT_DOWN;
  ui_page_update(&m,now,theme,reduced,"test","collection",first,count,columns,slots,unit);
  CHECK(ui_page_pixel_input(&m,now,reduced,"test",18,100,924,219,count,0,&first,&selected,0,128,128,&flags,&t));
  CHECK(m.pixels.dragging);
  for(int i=1;i<=7;i++){
   now+=16667;t.y=300-i*13;flags=UI_TOUCH_EVENT_MOVE;
   ui_page_pixel_input(&m,now,reduced,"test",18,100,924,219,count,0,&first,&selected,0,128,128,&flags,&t);
   CHECK(fabsf(m.pixels.position-i*13)<.01f);CHECK(!flags);
   UiPageView v=ui_page_current_view(&m,first,count,columns,slots,unit);
   CHECK(v.first< v.end&&v.end<=count);CHECK(fabsf(v.dy)>0.1f);
  }
  float before=m.pixels.position;now+=16667;flags=UI_TOUCH_EVENT_UP;
  ui_page_pixel_input(&m,now,reduced,"test",18,100,924,219,count,0,&first,&selected,0,128,128,&flags,&t);
  now+=16667;flags=0;ui_page_pixel_input(&m,now,reduced,"test",18,100,924,219,count,0,&first,&selected,0,128,128,&flags,&t);
  CHECK(reduced?m.pixels.position==before:m.pixels.position>before);
  CHECK(!m.pixels.dragging);
  for(int i=0;i<1000;i++){now+=16667;ui_page_pixel_input(&m,now,reduced,"test",18,100,924,219,count,0,&first,&selected,0,128,255,&flags,&t);}
  CHECK(m.pixels.position==m.pixels.maximum);CHECK(selected>=0&&selected<count);
  for(int i=0;i<1000;i++){now+=16667;ui_page_pixel_input(&m,now,reduced,"test",18,100,924,219,count,0,&first,&selected,0,128,0,&flags,&t);}
  CHECK(m.pixels.position==0&&first==0);
  now+=16667;ui_page_update(&m,now,theme,reduced,"test","new collection",0,4,columns,slots,unit);CHECK(!m.direct);
 }
 /* Mosaic's one-row deck drags horizontally in pixels, with retained cards. */
 for(int reduced=0;reduced<2;reduced++){
  UiPageMotion mosaic={0};int first=0,selected=0;uint64_t now=1000000;
  ui_page_update(&mosaic,now,6,reduced,"mosaic","cards",0,83,5,5,244);
  UiTouchEvent t={.down_x=800,.down_y=300,.x=800,.y=300};unsigned flags=UI_TOUCH_EVENT_DOWN;
  ui_page_pixel_input(&mosaic,now,reduced,"mosaic",18,210,924,244,83,0,&first,&selected,0,128,128,&flags,&t);
  now+=16667;t.x=613;flags=UI_TOUCH_EVENT_MOVE;
  ui_page_pixel_input(&mosaic,now,reduced,"mosaic",18,210,924,244,83,0,&first,&selected,0,128,128,&flags,&t);
  UiPageView view=ui_page_current_view(&mosaic,first,83,5,5,244);
  CHECK(fabsf(view.dx+187)<.01f&&view.dy==0&&view.first==0&&view.end==10);
  flags=UI_TOUCH_EVENT_UP;now+=16667;
  ui_page_pixel_input(&mosaic,now,reduced,"mosaic",18,210,924,244,83,0,&first,&selected,0,128,128,&flags,&t);
  CHECK(!mosaic.pixels.dragging);
 }
 /* Holding a dragged surface must not fling with stale movement velocity. */
 UiScrollView s={0};ui_scroll_bounds(&s,2000,200);ui_scroll_drag_begin(&s,300);ui_scroll_drag_move(&s,300,200,.016f);
 for(int i=0;i<60;i++)ui_scroll_drag_move(&s,300,200,.016f);
 ui_scroll_drag_end(&s,0);CHECK(!s.coasting);
 printf("Pixel viewport: %u checks, all eight themes, reduced motion, lists/grids, fractional rows, drag, inertia, stick limits, focus bounds and collection changes.\n",checks);
}
