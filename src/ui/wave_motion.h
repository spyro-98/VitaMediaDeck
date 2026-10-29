#ifndef VMD_UI_WAVE_MOTION_H
#define VMD_UI_WAVE_MOTION_H
#include <math.h>
#include <stdint.h>
/* Low-frequency, periodic value noise. Quintic interpolation keeps velocity
 * and acceleration continuous at lattice boundaries; no RNG calls per frame. */
static inline float ui_wave_hash(unsigned x,unsigned t,unsigned seed){
 uint32_t v=x*0x9e3779b9u+(t&31u)*0x85ebca6bu+seed;
 v=(v^(v>>16))*0x7feb352du;v=(v^(v>>15))*0x846ca68bu;v^=v>>16;
 return (v&0xffffu)/65535.f;
}
static inline float ui_wave_ease(float x){return x*x*x*(x*(x*6-15)+10);}
static inline float ui_wave_noise(float x,float t,unsigned seed){
 unsigned ix=(unsigned)floorf(x),it=(unsigned)floorf(t);
 float u=ui_wave_ease(x-floorf(x)),v=ui_wave_ease(t-floorf(t));
 float a=ui_wave_hash(ix,it,seed),b=ui_wave_hash(ix+1,it,seed);
 float c=ui_wave_hash(ix,it+1,seed),d=ui_wave_hash(ix+1,it+1,seed);
 return (a+(b-a)*u)*(1-v)+(c+(d-c)*u)*v;
}
static inline void ui_wave_sample(int wave,float x,float phase,unsigned seed,float *y,float *width){
 const float tau=6.28318530718f;
 float drift=phase*tau*(18+wave*2),angle=x*tau/(1260+wave*85.f);
 *y=104+wave*84+(x-480)*(wave%2?.09f:-.07f)+
    sinf(angle+drift+wave*.8f)*(22+wave*4)+sinf(angle*1.6f-drift+wave)*6;
 /* Broad changes: ~900 pixels in space, 12 seconds in time. */
 *width=.75f+1.05f*ui_wave_noise(x/900.f+wave*3,phase*32,seed+wave*701u);
}
#define UI_WAVE_SEGMENTS 61
#define UI_WAVE_VERTEX_LIMIT 8192
#endif
