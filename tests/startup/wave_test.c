#include "ui/wave_motion.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 unsigned checks=0;
 for(unsigned seed=1;seed<=7;seed++)for(int wave=0;wave<5;wave++)for(int tick=0;tick<200;tick++){
  float t=tick/200.f;
  for(int i=0;i<UI_WAVE_SEGMENTS;i++){
   float x=i*960.f/UI_WAVE_SEGMENTS,next=(i+1)*960.f/UI_WAVE_SEGMENTS;
   float a,wa,b,wb,mid,wm,after,wafter;
   ui_wave_sample(wave,x,t,seed,&a,&wa);ui_wave_sample(wave,next,t,seed,&b,&wb);
   ui_wave_sample(wave,(x+next)*.5f,t,seed,&mid,&wm);
   ui_wave_sample(wave,x,t+1.f/(60*384),seed,&after,&wafter);
   assert(isfinite(a)&&isfinite(wa)&&wa>=.75f&&wa<=1.8f);
   assert(fabsf(mid-(a+b)*.5f)<.12f); /* Subpixel chord error. */
   assert(fabsf(wb-wa)<.04f&&fabsf(wafter-wa)<.004f);
   assert(fabsf(after-a)<.4f);checks+=4;
  }
  float a,wa,b,wb;ui_wave_sample(wave,440,0,seed,&a,&wa);ui_wave_sample(wave,440,1,seed,&b,&wb);
  assert(fabsf(a-b)<.002f&&fabsf(wa-wb)<.002f);checks++;
 }
 printf("Wave motion: %u checks; bounded long-wavelength thickness, subpixel curvature, continuous slow animation and seamless periodic noise passed.\n",checks);
}
