#ifndef VMD_IMAGE_VIEW_CONTROLS_H
#define VMD_IMAGE_VIEW_CONTROLS_H
#include <math.h>
#include <psp2/ctrl.h>
#include "ui/touch.h"

#define UI_IMAGE_QUARTER_TURN 1.5707963267948966f

typedef struct {
 float zoom, pan_x, pan_y, rotation;
 UiTouchPoints previous_touch;
} UiImageViewState;

static inline float ui_image_limit(float value,float low,float high) {
 return value<low?low:value>high?high:value;
}
static inline float ui_image_fit(float width,float height,float rotation) {
 if(width<=0 || height<=0)return 1;
 float c=fabsf(cosf(rotation)),s=fabsf(sinf(rotation));
 float x=960.0f/(width*c+height*s),y=544.0f/(width*s+height*c);
 return fminf(x,y);
}
static inline void ui_image_clamp(UiImageViewState *v,float width,float height) {
 float scale=ui_image_fit(width,height,v->rotation)*v->zoom;
 float c=fabsf(cosf(v->rotation)),s=fabsf(sinf(v->rotation));
 float x=fmaxf(0,(scale*(width*c+height*s)-960.0f)*.5f);
 float y=fmaxf(0,(scale*(width*s+height*c)-544.0f)*.5f);
 v->pan_x=ui_image_limit(v->pan_x,-x,x);
 v->pan_y=ui_image_limit(v->pan_y,-y,y);
}
static inline void ui_image_zoom(UiImageViewState *v,float width,float height,
                                float factor,float anchor_x,float anchor_y) {
 float before=v->zoom;
 v->zoom=ui_image_limit(before*factor,.2f,8.0f);
 float applied=v->zoom/before;
 v->pan_x=anchor_x-480-(anchor_x-480-v->pan_x)*applied;
 v->pan_y=anchor_y-272-(anchor_y-272-v->pan_y)*applied;
 ui_image_clamp(v,width,height);
}
static inline void ui_image_rotate(UiImageViewState *v,float width,float height) {
 /* Store exact quarter turns; four presses return to zero without drift. */
 int turn=((int)lroundf(v->rotation/UI_IMAGE_QUARTER_TURN)+1)%4;
 v->rotation=turn*UI_IMAGE_QUARTER_TURN;
 float x=v->pan_x;v->pan_x=-v->pan_y;v->pan_y=x;
 v->previous_touch.count=0;
 ui_image_clamp(v,width,height);
}
static inline float ui_image_axis(unsigned char raw) {
 float axis=(float)raw-128;
 /* Ignore rest jitter, then ramp from zero instead of jumping at the edge. */
 float magnitude=fabsf(axis);
 if(magnitude<=40)return 0;
 return copysignf(fminf(1,(magnitude-40)/87.0f),axis);
}
static inline int ui_image_pan(UiImageViewState *v,float width,float height,
                               unsigned char lx,unsigned char ly,unsigned held,
                               float seconds) {
 float x=ui_image_axis(lx),y=ui_image_axis(ly);
 if(held&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))
  x=((held&SCE_CTRL_RIGHT)!=0)-((held&SCE_CTRL_LEFT)!=0);
 if(held&(SCE_CTRL_UP|SCE_CTRL_DOWN))
  y=((held&SCE_CTRL_DOWN)!=0)-((held&SCE_CTRL_UP)!=0);
 float length=sqrtf(x*x+y*y);
 if(length>1){x/=length;y/=length;}
 /* No inertia: release produces exactly zero displacement on the next sample.
  * Cap a delayed frame, so decoding a detail tile cannot fling the image. */
 seconds=ui_image_limit(seconds,0,.05f);
 v->pan_x-=x*360*seconds;v->pan_y-=y*360*seconds;
 ui_image_clamp(v,width,height);
 return x!=0 || y!=0;
}
static inline void ui_image_touch(UiImageViewState *v,float width,float height,
                                 const UiTouchPoints *touch) {
 const UiTouchPoints *old=&v->previous_touch;
 if(touch->count==1 && old->count==1 && touch->id[0]==old->id[0]) {
  v->pan_x+=touch->x[0]-old->x[0];
  v->pan_y+=touch->y[0]-old->y[0];
 } else if(touch->count==2 && old->count==2) {
  int a=touch->id[0]==old->id[0]?0:touch->id[0]==old->id[1]?1:-1;
  int b=touch->id[1]==old->id[0]?0:touch->id[1]==old->id[1]?1:-1;
  if(a>=0 && b>=0 && a!=b) {
   float old_dx=old->x[b]-old->x[a],old_dy=old->y[b]-old->y[a];
   float dx=touch->x[1]-touch->x[0],dy=touch->y[1]-touch->y[0];
   float before=sqrtf(old_dx*old_dx+old_dy*old_dy),after=sqrtf(dx*dx+dy*dy);
   float x=(touch->x[0]+touch->x[1])*.5f,y=(touch->y[0]+touch->y[1])*.5f;
   v->pan_x+=x-(old->x[0]+old->x[1])*.5f;
   v->pan_y+=y-(old->y[0]+old->y[1])*.5f;
   if(before>8 && after>8)ui_image_zoom(v,width,height,after/before,x,y);
  }
 }
 ui_image_clamp(v,width,height);
 v->previous_touch=*touch;
}
#endif
