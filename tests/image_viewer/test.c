#include <assert.h>
#include <stdio.h>
#include "ui/image_view_controls.h"
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
#define NEAR(a,b) CHECK(fabsf((a)-(b))<.02f)
int main(void) {
 UiImageViewState v={.zoom=2};
 /* Center jitter must never accumulate; panning stops as soon as input stops. */
 for(int x=88;x<=168;x++)for(int y=88;y<=168;y++) {
  CHECK(!ui_image_pan(&v,1920,1080,x,y,0,1.0f/60));
  NEAR(v.pan_x,0);NEAR(v.pan_y,0);
 }
 CHECK(ui_image_pan(&v,1920,1080,255,128,0,1.0f/60));NEAR(v.pan_x,-6);
 for(int i=0;i<600;i++)ui_image_pan(&v,1920,1080,128,128,0,1.0f/60);
 NEAR(v.pan_x,-6);
 /* Equal distance at 30/60 Hz; a delayed frame is capped to 18 pixels. */
 UiImageViewState a={.zoom=8},b=a;
 for(int i=0;i<30;i++)ui_image_pan(&a,1920,1080,255,128,0,1.0f/30);
 for(int i=0;i<60;i++)ui_image_pan(&b,1920,1080,255,128,0,1.0f/60);
 NEAR(a.pan_x,b.pan_x);NEAR(a.pan_x,-360);
 float x=a.pan_x;ui_image_pan(&a,1920,1080,255,128,0,9);NEAR(a.pan_x,x-18);
 ui_image_pan(&a,1920,1080,128,128,SCE_CTRL_LEFT|SCE_CTRL_RIGHT,1.0f/60);NEAR(a.pan_x,x-18);
 /* Panning cannot expose blank space beyond the zoomed image edge. */
 v=(UiImageViewState){.zoom=2,.pan_x=100000,.pan_y=-100000};
 ui_image_clamp(&v,1920,1080);NEAR(v.pan_x,480);NEAR(v.pan_y,-268);
 ui_image_zoom(&v,1920,1080,.5f,480,272);NEAR(v.pan_x,0);NEAR(v.pan_y,0);
 for(int i=0;i<1000;i++) {
  ui_image_rotate(&v,1920,1080);CHECK(v.rotation==((i+1)%4)*UI_IMAGE_QUARTER_TURN);
  float scale=ui_image_fit(1920,1080,v.rotation);
  CHECK(scale*(1920*fabsf(cosf(v.rotation))+1080*fabsf(sinf(v.rotation)))<=960.01f);
  CHECK(scale*(1920*fabsf(sinf(v.rotation))+1080*fabsf(cosf(v.rotation)))<=544.01f);
 }
 NEAR(v.rotation,0);
 /* A pinch never rotates; swapped/replaced touch identities cannot fling it. */
 v=(UiImageViewState){.zoom=2};
 UiTouchPoints touch={.count=2,.x={400,600},.y={272,272},.id={1,2}};
 ui_image_touch(&v,1920,1080,&touch);
 touch.x[0]=300;touch.x[1]=700;ui_image_touch(&v,1920,1080,&touch);NEAR(v.zoom,4);NEAR(v.rotation,0);
 x=v.pan_x;touch.x[0]=700;touch.x[1]=300;touch.id[0]=2;touch.id[1]=1;
 ui_image_touch(&v,1920,1080,&touch);NEAR(v.zoom,4);NEAR(v.pan_x,x);
 touch.x[0]=10;touch.id[0]=3;ui_image_touch(&v,1920,1080,&touch);NEAR(v.zoom,4);NEAR(v.pan_x,x);
 touch.count=1;touch.x[0]=100;ui_image_touch(&v,1920,1080,&touch);NEAR(v.pan_x,x);
 touch.x[0]=110;ui_image_touch(&v,1920,1080,&touch);NEAR(v.pan_x,x+10);
 touch.count=0;ui_image_touch(&v,1920,1080,&touch);
 for(int i=0;i<600;i++)ui_image_touch(&v,1920,1080,&touch);
 NEAR(v.pan_x,x+10);
 printf("PASS: %u image controls checks\n",checks);
}
