#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
#include "ui/theme_home.h"
#include "ui/theme_scene.h"
#include "ui/wave_motion.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include "ui/components.h"
#include "ui/runtime.h"
#include "i18n/i18n.h"

UiThemeRect ui_theme_home_tab_rect(int theme,int p,int expanded){
 (void)expanded; /* Non-Legacy shells never collapse into the status bar. */
 if(p<0)p=0;
 if(p>5)p=5;
 if(theme==VT_UI_THEME_GHOST_MESH){
  float angle=-1.57079632679f+p*1.0471975512f;
  return (UiThemeRect){(int)roundf(690+cosf(angle)*164)-85,(int)roundf(272+sinf(angle)*164)-20,170,40};
 }
 if(theme==VT_UI_THEME_DECK_90)return (UiThemeRect){28,112+p*54,188,44};
 if(theme==VT_UI_THEME_APERTURE)return (UiThemeRect){18+p*155,420,147,38};
 if(theme==VT_UI_THEME_RELAY)return (UiThemeRect){28+(p%2)*18,82+p*57,172,44};
 if(theme==VT_UI_THEME_MOSAIC)return (UiThemeRect){18+p*155,64+(p%2)*4,147,42};
 return (UiThemeRect){18+p*155,62,147,40};
}
int ui_theme_home_menu_columns(int theme){
 return theme==VT_UI_THEME_RELAY||theme==VT_UI_THEME_DECK_90?1:6;
}
UiThemeHomeLayout ui_theme_home_layout(int theme,int bottom){
 UiThemeHomeLayout l={18,62,924,52,8,1};
 switch(theme){
  case VT_UI_THEME_GHOST_MESH:l=(UiThemeHomeLayout){28,174,388,52,6,1};break;
  case VT_UI_THEME_DECK_90:l=(UiThemeHomeLayout){254,164,670,52,7,1};break;
  case VT_UI_THEME_SIGNAL:l=(UiThemeHomeLayout){28,214,442,122,4,2};break;
  case VT_UI_THEME_APERTURE:l=(UiThemeHomeLayout){46,166,868,228,1,1};break;
  case VT_UI_THEME_RELAY:l=(UiThemeHomeLayout){264,168,282,128,4,2};break;
  case VT_UI_THEME_MOSAIC:l=(UiThemeHomeLayout){24,210,172,244,5,5};break;
 }
 int limit=theme==VT_UI_THEME_APERTURE?398:bottom-38;
 int available=(limit-l.y)/l.step*l.columns;
 if(available<l.columns)available=l.columns;
 if(l.rows>available)l.rows=available;
 return l;
}
UiThemeRect ui_theme_home_item_rect(int theme,int slot,int bottom){
 UiThemeHomeLayout l=ui_theme_home_layout(theme,bottom);
 int row=slot/l.columns,col=slot%l.columns;if(col<0){col+=l.columns;row--;}
 UiThemeRect r={l.x+col*(l.w+12),l.y+row*l.step,l.w,l.step-8};
 if(theme==VT_UI_THEME_RELAY){
  r.x=264+col*326;r.y=168+row*128+col*28;r.h=96;
 }else if(theme==VT_UI_THEME_MOSAIC){
  r.x+=row*960;r.y=210+abs(col-2)*20;
  r.h=bottom-38-250;if(r.h>244)r.h=244;if(r.h<110)r.h=110;
 }
 return ui_theme_page_rect(UI_SCENE_HOME,r);
}
int ui_theme_home_top(int theme,int selected,int top,int bottom){
 UiThemeHomeLayout l=ui_theme_home_layout(theme,bottom);
 if(selected<0)selected=0;
 if(top<0)top=0;
 top-=top%l.columns;
 if(selected<top)top=selected-selected%l.columns;
 if(selected>=top+l.rows)top=(selected/l.columns-l.rows/l.columns+1)*l.columns;
 return top;
}
/* Native game-interface material pass: bounded geometry, no frame allocations,
 * framebuffers, shaders, timer threads, random particles or network data. */
#include <psp2/kernel/processmgr.h>
#include "settings/preferences.h"
static struct {int theme,selection,ready;uint64_t changed;} fx_state;
static unsigned fx_alpha(unsigned color,unsigned alpha){return (color&0xffffffU)|((alpha&255U)<<24);}
static unsigned fx_mix(unsigned a,unsigned b,unsigned amount){
 unsigned r=(((a&255)*(255-amount)+(b&255)*amount)/255);
 unsigned g=((((a>>8)&255)*(255-amount)+((b>>8)&255)*amount)/255);
 unsigned blue=((((a>>16)&255)*(255-amount)+((b>>16)&255)*amount)/255);
 return RGBA8(r,g,blue,255);
}
static float fx_phase(uint64_t period){
 return vt_preferences_reduce_motion()?0.0f:(float)(sceKernelGetProcessTimeWide()%period)/(float)period;
}
static void fx_select(int theme,int selection){
 if(!fx_state.ready||fx_state.theme!=theme||fx_state.selection!=selection){
  fx_state.theme=theme;fx_state.selection=selection;fx_state.ready=1;fx_state.changed=sceKernelGetProcessTimeWide();
 }
}
static float fx_impulse(void){
 if(vt_preferences_reduce_motion()||!fx_state.ready)return 0;
 uint64_t now=sceKernelGetProcessTimeWide();if(now<fx_state.changed)return 0;
 float t=(float)(now-fx_state.changed)/300000.0f;
 return t>=1?0:(1-t)*(1-t);
}
/* Stepped alpha is spatial feathering, not a per-pixel CPU blur. */
static void fx_shadow(float x,float y,float w,float h,int dark){
 for(int i=3;i>=1;i--)vita2d_draw_rectangle(x+i*2,y+i*3,w,h,RGBA8(0,0,0,dark?14:9));
}
static void fx_frame(float x,float y,float w,float h,unsigned color){
 vita2d_draw_rectangle(x,y,w,1,color);vita2d_draw_rectangle(x,y+h-1,w,1,color);
 vita2d_draw_rectangle(x,y,1,h,color);vita2d_draw_rectangle(x+w-1,y,1,h,color);
}
/* Dark console poster: original line geometry, no textures or new shader.
 * Fixed upper bound 8,192 vertices / 131,072 pool bytes, one GPU draw. */
static void vmd_quad(vita2d_color_vertex *v, unsigned *count,
                     const float x[4], const float y[4], unsigned color) {
    static const unsigned order[6] = {0,1,2,0,2,3};
    for (int n=0;n<6;n++) {
        unsigned j=order[n];
        v[(*count)++] = (vita2d_color_vertex){x[j],fmaxf(0,fminf(544,y[j])),.5f,color};
    }
}
static void vmd_symbol(vita2d_color_vertex *v,unsigned *count,int symbol,
                       float x,float y,unsigned color) {
    /* Four original glyph meshes, already rotated/outlined: no per-frame
     * glyph tessellation, square root or trigonometry. */
    static const unsigned first[4]={0,12,84,102},size[4]={12,72,18,24};
    static const float glyph[126][2]={
        {-10.574049f,-5.458554f},
        {9.859791f,6.662646f},
        {10.574049f,5.458554f},
        {-10.574049f,-5.458554f},
        {10.574049f,5.458554f},
        {-9.859791f,-6.662646f},
        {5.458554f,-10.574049f},
        {-6.662646f,9.859791f},
        {-5.458554f,10.574049f},
        {5.458554f,-10.574049f},
        {-5.458554f,10.574049f},
        {6.662646f,-9.859791f},
        {10.926849f,-2.977060f},
        {10.853553f,3.234084f},
        {12.253455f,3.250604f},
        {10.926849f,-2.977060f},
        {12.253455f,3.250604f},
        {12.326751f,-2.960540f},
        {10.951459f,2.885215f},
        {7.782411f,8.227575f},
        {8.986502f,8.941833f},
        {10.951459f,2.885215f},
        {8.986502f,8.941833f},
        {12.155550f,3.599473f},
        {8.041634f,7.974399f},
        {2.625978f,11.016494f},
        {3.311622f,12.237106f},
        {8.041634f,7.974399f},
        {3.311622f,12.237106f},
        {8.727279f,9.195010f},
        {2.977060f,10.926849f},
        {-3.234084f,10.853553f},
        {-3.250604f,12.253455f},
        {2.977060f,10.926849f},
        {-3.250604f,12.253455f},
        {2.960540f,12.326751f},
        {-2.885215f,10.951459f},
        {-8.227575f,7.782411f},
        {-8.941833f,8.986502f},
        {-2.885215f,10.951459f},
        {-8.941833f,8.986502f},
        {-3.599473f,12.155550f},
        {-7.974399f,8.041634f},
        {-11.016494f,2.625978f},
        {-12.237106f,3.311622f},
        {-7.974399f,8.041634f},
        {-12.237106f,3.311622f},
        {-9.195010f,8.727279f},
        {-10.926849f,2.977060f},
        {-10.853553f,-3.234084f},
        {-12.253455f,-3.250604f},
        {-10.926849f,2.977060f},
        {-12.253455f,-3.250604f},
        {-12.326751f,2.960540f},
        {-10.951459f,-2.885215f},
        {-7.782411f,-8.227575f},
        {-8.986502f,-8.941833f},
        {-10.951459f,-2.885215f},
        {-8.986502f,-8.941833f},
        {-12.155550f,-3.599473f},
        {-8.041634f,-7.974399f},
        {-2.625978f,-11.016494f},
        {-3.311622f,-12.237106f},
        {-8.041634f,-7.974399f},
        {-3.311622f,-12.237106f},
        {-8.727279f,-9.195010f},
        {-2.977060f,-10.926849f},
        {3.234084f,-10.853553f},
        {3.250604f,-12.253455f},
        {-2.977060f,-10.926849f},
        {3.250604f,-12.253455f},
        {-2.960540f,-12.326751f},
        {2.885215f,-10.951459f},
        {8.227575f,-7.782411f},
        {8.941833f,-8.986502f},
        {2.885215f,-10.951459f},
        {8.941833f,-8.986502f},
        {3.599473f,-12.155550f},
        {7.974399f,-8.041634f},
        {11.016494f,-2.625978f},
        {12.237106f,-3.311622f},
        {7.974399f,-8.041634f},
        {12.237106f,-3.311622f},
        {9.195010f,-8.727279f},
        {-3.469581f,-11.137700f},
        {11.052718f,3.731445f},
        {12.054280f,2.753245f},
        {-3.469581f,-11.137700f},
        {12.054280f,2.753245f},
        {-2.468019f,-12.115900f},
        {11.380317f,2.564106f},
        {-8.757882f,7.706216f},
        {-8.411517f,9.062694f},
        {11.380317f,2.564106f},
        {-8.411517f,9.062694f},
        {11.726682f,3.920584f},
        {-7.910736f,8.573594f},
        {-2.294837f,-11.437661f},
        {-3.642763f,-11.815939f},
        {-7.910736f,8.573594f},
        {-3.642763f,-11.815939f},
        {-9.258663f,8.195316f},
        {-10.335650f,-5.555521f},
        {6.406942f,-9.830593f},
        {6.060578f,-11.187071f},
        {-10.335650f,-5.555521f},
        {6.060578f,-11.187071f},
        {-10.682014f,-6.911999f},
        {5.555521f,-10.335650f},
        {9.830593f,6.406942f},
        {11.187071f,6.060578f},
        {5.555521f,-10.335650f},
        {11.187071f,6.060578f},
        {6.911999f,-10.682014f},
        {10.335650f,5.555521f},
        {-6.406942f,9.830593f},
        {-6.060578f,11.187071f},
        {10.335650f,5.555521f},
        {-6.060578f,11.187071f},
        {10.682014f,6.911999f},
        {-5.555521f,10.335650f},
        {-9.830593f,-6.406942f},
        {-11.187071f,-6.060578f},
        {-5.555521f,10.335650f},
        {-11.187071f,-6.060578f},
        {-6.911999f,10.682014f}
    };
    for(unsigned n=first[symbol];n<first[symbol]+size[symbol];n++)
        v[(*count)++]=(vita2d_color_vertex){x+glyph[n][0],fmaxf(0,fminf(544,y+glyph[n][1])),.5f,color};
}
static void vmd_wave_background(unsigned base) {
    (void)base;
#ifdef __vita__
    vita2d_color_vertex *vertices=vita2d_pool_memalign(UI_WAVE_VERTEX_LIMIT*sizeof(*vertices),16);
    if(!vertices)return;
#else
    vita2d_color_vertex storage[UI_WAVE_VERTEX_LIMIT],*vertices=storage;
#endif
    unsigned count=0;
    for(int band=0;band<12;band++) {
        float top=band*(544.f/12),bottom=(band+1)*(544.f/12);
        const float x[4]={0,960,960,0},y[4]={top,top,bottom,bottom};
        vmd_quad(vertices,&count,x,y,fx_mix(RGBA8(2,7,18,255),RGBA8(5,15,31,255),band*18));
    }
    float poster=fx_phase(24000000ULL)*4;
    int step=(int)poster;
    float offset=poster-step;
    for(int row=-1;row<7;row++)for(int col=-1;col<8;col++) {
        int symbol=(row+col-step+40)%4;
        vmd_symbol(vertices,&count,symbol,col*128.f+row*30+offset*128,
            row*110.f-col*32-offset*32+36,RGBA8(68,178,224,34));
    }
    static unsigned seed;
    if(!seed)seed=(unsigned)sceKernelGetProcessTimeWide()|1u;
    float phase=fx_phase(384000000ULL);
    for(int wave=0;wave<5;wave++) {
        float x0=0,y0,width0;ui_wave_sample(wave,x0,phase,seed,&y0,&width0);
        for(int i=0;i<UI_WAVE_SEGMENTS;i++) {
            float x1=(i+1)*(960.f/UI_WAVE_SEGMENTS),y1,width1;
            ui_wave_sample(wave,x1,phase,seed,&y1,&width1);
            for(int lane=0;lane<3;lane++) {
                static const float half_width[3]={5.5f,2.5f,.65f};
                static const unsigned opacity[3]={9,24,120};
                float h0=half_width[lane]*width0,h1=half_width[lane]*width1;
                const float qx[4]={x0,x1,x1,x0},qy[4]={y0-h0,y1-h1,y1+h1,y0+h0};
                vmd_quad(vertices,&count,qx,qy,RGBA8(40,198,242,opacity[lane]));
            }
            x0=x1;y0=y1;width0=width1;
        }
    }
#ifdef VMD_ENABLE_PAF_NATIVE
    ui_native_vita_cover(0,0,960,544,RGBA8(2,7,18,255));
#endif
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,vertices,count);
}
void ui_theme_fx_background(unsigned base){
 int theme=ui_theme_id();
 if (theme == VT_UI_THEME_LEGACY_VITA) {
     vmd_wave_background(base);
     return;
 }
 if (vt_ui_theme_legacy_layout(theme)) {
     float phase = fx_phase(10000000ULL);
     unsigned alpha = (unsigned)(10 + 6 * (1 - cosf(phase * 6.28318530718f)));
     vita2d_draw_rectangle(0, 54, 1, 490, fx_alpha(VT_THEME_COLD, alpha));
     vita2d_draw_rectangle(959, 54, 1, 490, fx_alpha(VT_THEME_FOCUS, alpha));
     return;
 }
 unsigned accent=VT_THEME_SIGNAL_BRIGHT;
 /* The light horizon and floor share a vanishing point: real spatial structure
  * rather than a translucent wash behind otherwise flat cards. */
 for(int i=0;i<14;i++){
  unsigned light=(unsigned)(theme==VT_UI_THEME_SIGNAL?3+i/3:14-i);
  vita2d_draw_rectangle(0,54+i*35,960,35,fx_mix(base,accent,light));
 }
 float drift=fx_phase(14000000ULL);
 if(theme==VT_UI_THEME_GHOST_MESH||theme==VT_UI_THEME_APERTURE){
  float horizon=theme==VT_UI_THEME_GHOST_MESH?302:350;
  for(int i=-5;i<=5;i++)vita2d_draw_line(690+i*19,horizon,480+i*160,544,fx_alpha(accent,19));
  for(int i=1;i<=7;i++){
   float f=((float)i+drift)/8;float y=horizon+(544-horizon)*f*f;
   vita2d_draw_line(0,y,960,y,fx_alpha(accent,16));
  }
  vita2d_draw_line(0,horizon,960,horizon,fx_alpha(accent,25));
 }else if(theme==VT_UI_THEME_DECK_90){
  for(int y=58;y<544;y+=12)vita2d_draw_rectangle(0,y,960,1,RGBA8(0,0,0,22));
  float y=54+fx_phase(8500000ULL)*490;
  for(int i=0;i<8;i++)vita2d_draw_rectangle(0,y+i*3,960,2,fx_alpha(accent,(unsigned)(9-i)));
  fx_frame(14,62,932,464,fx_alpha(accent,50));
 }else if(theme==VT_UI_THEME_SIGNAL){
  for(int i=0;i<9;i++)vita2d_draw_line(400+i*84,54,80+i*84,544,fx_alpha(accent,18));
  float x=fx_phase(10000000ULL)*1050-90;
  for(int i=0;i<5;i++)vita2d_draw_line(x+i*3,54,x-320+i*3,544,fx_alpha(accent,45-i*7));
 }else if(theme==VT_UI_THEME_RELAY){
  for(int i=0;i<5;i++){
   float x=24+i*230;
   vita2d_draw_line(x,54,x,544,fx_alpha(accent,12));
   vita2d_draw_line(x,480,x+62,480,fx_alpha(accent,50));
  }
  float travel=fx_phase(9000000ULL)*900;
  vita2d_draw_rectangle(28+travel,482,24,2,fx_alpha(accent,120));
 }else if(theme==VT_UI_THEME_MOSAIC){
  for(int i=0;i<4;i++){
   float x=16+i*306;
   vita2d_draw_line(x,54,x-60,544,fx_alpha(accent,22));
  }
  float y=54+fx_phase(16000000ULL)*490;
  vita2d_draw_line(0,y,960,y-120,fx_alpha(accent,35));
 }
 ui_theme_scene_background();
 /* Opposing edge lights define the frame without claiming fake telemetry. */
 vita2d_draw_rectangle(0,54,3,490,fx_alpha(accent,38));
 vita2d_draw_rectangle(957,54,3,490,fx_alpha(VT_THEME_COLD,38));
}
void ui_theme_fx_surface(float x,float y,float w,float h,unsigned fill,int selected){
    int theme = ui_theme_id();
    if (vt_ui_theme_legacy_layout(theme) || w < 60 || h < 30)
        return;
    fx_shadow(x, y, w, h, theme == VT_UI_THEME_SIGNAL);
    unsigned tint = theme == VT_UI_THEME_DECK_90 ? VT_THEME_SIGNAL_BRIGHT : VT_THEME_COLD;
    for (int i = 0; i < 5; i++)
        vita2d_draw_rectangle(x, y + i * (h / 5), w, h / 5 + 1,
                              fx_mix(fill, tint, (unsigned)((selected ? 22 : 10) * (5 - i) / 5)));
    vita2d_draw_line(x + 7, y, x + w - 10, y, fx_alpha(VT_THEME_TEXT, selected ? 100 : 42));
    vita2d_draw_line(x + w - 1, y + 8, x + w - 1, y + h - 5, RGBA8(0, 0, 0, 80));
    if (theme == VT_UI_THEME_DECK_90) {
        vita2d_draw_rectangle(x + 2, y + 2, 2, h - 4, RGBA8(0, 0, 0, 100));
        vita2d_draw_rectangle(x + 5, y + h - 3, w - 10, 1, fx_alpha(tint, 60));
    } else {
        /* Faceted specular lip: a narrow reflection, independent of text/artwork. */
        for (int i = 0; i < 4; i++)
            vita2d_draw_rectangle(
                x + 8 + i * 3, y + 2 + i, w - 24 - i * 6, 1,
                fx_alpha(VT_THEME_TEXT, (unsigned)(selected ? 38 - i * 7 : 16 - i * 3)));
        vita2d_draw_line(x, y + 9, x + 9, y, fx_alpha(tint, selected ? 210 : 75));
        vita2d_draw_line(x + w - 10, y + h - 1, x + w - 1, y + h - 10, fx_alpha(tint, 90));
    }
 if(selected){
  float pulse=fx_impulse();float sweep=vt_preferences_reduce_motion()?0.5f:fx_phase(3600000ULL);
  vita2d_draw_rectangle(x+8+sweep*(w-44),y+h-2,28,2,fx_alpha(tint,180));
  if(pulse>0)vita2d_draw_rectangle(x+3,y+3,2,h-6,fx_alpha(tint,(unsigned)(pulse*180)));
 }
}
void ui_theme_service_icon(float x,float y,float w,float h,int service) {
 unsigned light=service==1?VT_THEME_SIGNAL_BRIGHT:VT_THEME_COLD;
 float sx=w/136,sy=h/108;
 for(int i=0;i<3;i++){
  float yy=y+(20+i*25)*sy;
  vita2d_draw_rectangle(x+15*sx,yy,106*sx,18*sy,VT_THEME_SURFACE_RAISED);
  vita2d_draw_rectangle(x+17*sx,yy,102*sx,1,fx_alpha(light,140));
  vita2d_draw_rectangle(x+104*sx,yy+6*sy,7*sx,6*sy,light);
 }
 if(service==1){
  vita2d_draw_rectangle(x+44*sx,y+27*sy,3*sx,40*sy,light);
  vita2d_draw_rectangle(x+47*sx,y+27*sy,20*sx,3*sy,light);
  vita2d_draw_rectangle(x+32*sx,y+63*sy,15*sx,7*sy,light);
 }else{
  vita2d_draw_line(x+35*sx,y+63*sy,x+52*sx,y+42*sy,light);
  vita2d_draw_line(x+52*sx,y+42*sy,x+70*sx,y+63*sy,light);
  vita2d_draw_line(x+35*sx,y+63*sy,x+77*sx,y+63*sy,light);
  vita2d_draw_rectangle(x+66*sx,y+34*sy,6*sx,6*sy,light);
 }
}
static void corners(UiThemeRect r,unsigned color){
 const int n=12;
 vita2d_draw_rectangle(r.x,r.y,n,2,color);vita2d_draw_rectangle(r.x,r.y,2,n,color);
 vita2d_draw_rectangle(r.x+r.w-n,r.y+r.h-2,n,2,color);
 vita2d_draw_rectangle(r.x+r.w-2,r.y+r.h-n,2,n,color);
}
static void fx_category_icon(float x,float y,float scale,int category,unsigned color){
 if(category==0){
  for(int i=0;i<4;i++)vita2d_draw_rectangle(x+(i%2)*12*scale,y+(i/2)*12*scale,8*scale,8*scale,color);
 }else if(category==1){
  fx_frame(x,y,26*scale,19*scale,color);
  vita2d_draw_line(x+10*scale,y+5*scale,x+17*scale,y+9*scale,color);
  vita2d_draw_line(x+17*scale,y+9*scale,x+10*scale,y+14*scale,color);
  vita2d_draw_line(x+10*scale,y+5*scale,x+10*scale,y+14*scale,color);
 }else if(category==2){
  vita2d_draw_rectangle(x+13*scale,y,3*scale,18*scale,color);
  vita2d_draw_rectangle(x+16*scale,y,9*scale,3*scale,color);
  vita2d_draw_rectangle(x+5*scale,y+16*scale,11*scale,5*scale,color);
 }else if(category==3){
  for(int i=0;i<4;i++)vita2d_draw_rectangle(x+i*7*scale,y+(3-i)*4*scale,3*scale,(6+i*4)*scale,color);
 }else if(category==4){
  fx_frame(x+8*scale,y,10*scale,14*scale,color);
  vita2d_draw_line(x+4*scale,y+6*scale,x+4*scale,y+17*scale,color);
  vita2d_draw_line(x+4*scale,y+17*scale,x+22*scale,y+17*scale,color);
  vita2d_draw_line(x+22*scale,y+6*scale,x+22*scale,y+17*scale,color);
  vita2d_draw_rectangle(x+12*scale,y+17*scale,2*scale,5*scale,color);
 }else{
  fx_frame(x,y,26*scale,21*scale,color);
  vita2d_draw_line(x+3*scale,y+17*scale,x+11*scale,y+8*scale,color);
  vita2d_draw_line(x+11*scale,y+8*scale,x+21*scale,y+17*scale,color);
  vita2d_draw_rectangle(x+18*scale,y+4*scale,3*scale,3*scale,color);
 }
}

void ui_theme_home_menu(int theme,int selected,const char *const labels[6],const unsigned char *categories,int focused,UiFocusMotion *motion){
 (void)categories;fx_select(theme,selected);
 UiThemeRect target=ui_theme_home_tab_rect(theme,selected,1);
 if(focused){ui_focus_motion_tick(motion,target.x,target.y,target.w,target.h);ui_menu_focus_draw(motion);}
 for(int i=0;i<6;i++){
  UiThemeRect r=ui_theme_home_tab_rect(theme,i,1);
  ui_menu_row(r.x,r.y,r.w,r.h,0,1);
  ui_menu_text(r.x+12,r.y+r.h*.5f+7,r.w-24,labels[i],i==selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }
}
void ui_theme_home_heading(const char *title,const char *path,int bottom){
 int theme=ui_theme_id();UiThemeHomeLayout l=ui_theme_home_layout(theme,bottom);
 ui_theme_scene_begin(UI_SCENE_HOME,title);
 unsigned size=theme==VT_UI_THEME_SIGNAL||theme==VT_UI_THEME_APERTURE||theme==VT_UI_THEME_GHOST_MESH?UI_FONT_HERO:UI_FONT_DISPLAY;
 int y=theme==VT_UI_THEME_SIGNAL?162:theme==VT_UI_THEME_MOSAIC?151:theme==VT_UI_THEME_GHOST_MESH?122:113;
 if(theme==VT_UI_THEME_MOSAIC){l.w=912;size=UI_FONT_DISPLAY;}
 if(theme==VT_UI_THEME_RELAY)l.w=624;
 ui_text_line(l.x,y,l.w,title,theme==VT_UI_THEME_GHOST_MESH?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT,size);
 if(theme!=VT_UI_THEME_MOSAIC)ui_text_line(l.x,y+30,l.w,path?path:"",VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 else ui_text_line(l.x,y+27,l.w,path?path:"",VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 if(theme!=VT_UI_THEME_MOSAIC)vita2d_draw_rectangle(l.x,l.y-13,l.w,1,VT_THEME_BORDER);
 if(theme==VT_UI_THEME_RELAY){
  vita2d_set_clip_rectangle(0,l.y-4,960,bottom);vita2d_enable_clipping();
  for(int i=1;i<4;i++){
   UiThemeRect a=ui_theme_home_item_rect(theme,i-1,bottom),b=ui_theme_home_item_rect(theme,i,bottom);
   float x=a.x+a.w*.5f,y=a.y+a.h*.5f,nx=b.x+b.w*.5f,ny=b.y+b.h*.5f;
   vita2d_draw_line(x,y,nx,y,VT_THEME_COLD);vita2d_draw_line(nx,y,nx,ny,VT_THEME_COLD);
  }
  vita2d_disable_clipping();
 }
 if(theme==VT_UI_THEME_SIGNAL){
  /* Parallel edges and constant thickness: an intentional diagonal, no stair gaps. */
  for(int i=0;i<19;i++)vita2d_draw_rectangle(860+i,126+i,38,1,VT_THEME_SIGNAL_BRIGHT);
 }
}
static void theme_home_row(int slot,int ordinal,const char *name,const char *detail,int selected,int focused,int bottom,vita2d_texture *art,const char *placeholder){
 int theme=ui_theme_id();UiThemeRect r=ui_theme_home_item_rect(theme,slot,bottom);
 if(r.h>80){ui_theme_browser_tile(UI_SCENE_HOME,r,slot,selected&&focused,art,name,detail,!placeholder||!strcmp(placeholder,"DIR"));return;}
 unsigned fg=VT_THEME_TEXT,muted=VT_THEME_TEXT_MUTED;
 unsigned fill=selected&&focused?(theme==VT_UI_THEME_SIGNAL?VT_THEME_TEXT:VT_THEME_SURFACE_RAISED):VT_THEME_SURFACE;
 ui_theme_scene_card(UI_SCENE_HOME,r,selected&&focused,slot);
 ui_theme_fx_surface(r.x+4,r.y+3,r.w-8,r.h-6,fill,selected&&focused);
 if(selected&&focused&&theme==VT_UI_THEME_SIGNAL)fg=muted=VT_THEME_BG;
 int inset=theme==VT_UI_THEME_SIGNAL?48:14;
 if(theme==VT_UI_THEME_SIGNAL&&!placeholder){char n[16];snprintf(n,sizeof(n),"%02d",ordinal+1);
  ui_text_line(r.x+8,r.y+32,38,n,selected&&focused?fg:VT_THEME_SIGNAL_BRIGHT,UI_FONT_BODY);}
 if(placeholder){
  int size=r.h-12;if(size>58)size=58;
  ui_artwork_tile(r.x+8,r.y+(r.h-size)*.5f,size,art,placeholder);inset=size+20;
 }
 int title_y=r.h>80?r.h-38:21,detail_y=r.h>80?r.h-14:40;
 if(r.h>80&&!placeholder)fx_category_icon(r.x+14,r.y+14,1.25f,0,VT_THEME_COLD);
 ui_text_line(r.x+inset,r.y+title_y,r.w-inset-12,name,fg,UI_FONT_SMALL);
 ui_text_line(r.x+inset,r.y+detail_y,r.w-inset-12,detail,muted,UI_FONT_SMALL);
 if(theme==VT_UI_THEME_MOSAIC)corners(r,selected&&focused?VT_THEME_FOCUS:VT_THEME_BORDER_DIM);
 else vita2d_draw_rectangle(r.x,r.y+r.h,r.w,1,VT_THEME_BORDER_DIM);
}
void ui_theme_home_row(int slot,int ordinal,const char *name,const char *detail,int selected,int focused,int bottom){
 theme_home_row(slot,ordinal,name,detail,selected,focused,bottom,NULL,NULL);
}
void ui_theme_home_row_art(int slot,int ordinal,const char *name,const char *detail,int selected,int focused,int bottom,vita2d_texture *art,const char *placeholder){
 theme_home_row(slot,ordinal,name,detail,selected,focused,bottom,art,placeholder);
}
void ui_theme_home_preview(vita2d_texture *texture,const char *label,int bottom,int service){
 /* Artwork belongs to the large selected item, never to a second overlay. */
 (void)texture;(void)label;(void)bottom;(void)service;
}
void ui_theme_home_position(int selected,int count,int bottom){
 int theme=ui_theme_id();
 if(theme<VT_UI_THEME_APERTURE||count<=0)return;
 char text[40];snprintf(text,sizeof(text),"%d / %d",selected+1,count);
 int y=bottom-10;
 if(theme==VT_UI_THEME_APERTURE){UiThemeHomeLayout l=ui_theme_home_layout(theme,bottom);y=l.y+l.step-30;}
 ui_text_line(822,y,94,text,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
}
void ui_theme_home_service(int slot,int bottom,int service){
 int theme=ui_theme_id();if(theme<VT_UI_THEME_APERTURE)return;
 UiThemeRect r=ui_theme_home_item_rect(theme,slot,bottom);
 int size,x,y;
 if(theme==VT_UI_THEME_APERTURE){size=r.h-32;x=r.x+18;y=r.y+16;}
 else if(theme==VT_UI_THEME_RELAY){size=r.h-28;x=r.x+14;y=r.y+14;}
 else{size=r.w-32;if(size>r.h-78)size=r.h-78;x=r.x+(r.w-size)/2;y=r.y+20;}
 vita2d_draw_rectangle(x,y,size,size,VT_THEME_BG);
 ui_theme_service_icon(x+size*.12f,y+size*.12f,size*.76f,size*.76f,service);
}
const char *ui_theme_home_controls_hint(int sections_focused){
 static const VtStringId hints[VT_UI_THEME_COUNT][2]={
  {VT_STR_HOME_HELP_TOP_CONTENT,VT_STR_HOME_HELP_LEGACY_TABS},
  {VT_STR_HOME_GHOST_CONTENT_HINT,VT_STR_HOME_GHOST_WHEEL_HINT},
  {VT_STR_HOME_HELP_DECK_CONTENT,VT_STR_HOME_HELP_SIDE_TABS},
  {VT_STR_HOME_HELP_TOP_CONTENT,VT_STR_HOME_HELP_TOP_TABS},
  {VT_STR_HOME_HELP_APERTURE_CONTENT,VT_STR_HOME_HELP_APERTURE_TABS},
  {VT_STR_HOME_HELP_RELAY_CONTENT,VT_STR_HOME_HELP_SIDE_TABS},
  {VT_STR_HOME_HELP_MOSAIC_CONTENT,VT_STR_HOME_HELP_TOP_TABS}
 };
 return vt_i18n_str(
     hints[vt_ui_theme_legacy_layout(ui_theme_id()) ? VT_UI_THEME_LEGACY : ui_theme_id()]
          [!!sections_focused]);
}
void ui_theme_home_footer_with_focus(const char *path,int bottom,int sections_focused){
#ifdef VMD_ENABLE_PAF_NATIVE
#endif
 UiThemeHomeLayout l=ui_theme_home_layout(ui_theme_id(),bottom);
 const char *hint=ui_theme_home_controls_hint(sections_focused);
 if(ui_theme_id()==VT_UI_THEME_GHOST_MESH){
  ui_text_line(l.x,bottom-26,l.w,vt_i18n_str(VT_STR_HOME_GHOST_WHEEL_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  ui_text_line(l.x,bottom-8,l.w,ui_theme_home_controls_hint(0),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }else if(ui_theme_id()==VT_UI_THEME_APERTURE){
  ui_text_line(l.x,408,868,hint,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }else{
  /* The full free rail spans columns; a Mosaic card's width is not the footer. */
  int width=942-l.x;
  ui_text_line(l.x,bottom-26,width,path?path:"",VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  int hint_width=(ui_theme_id()==VT_UI_THEME_RELAY||ui_theme_id()==VT_UI_THEME_MOSAIC?804:942)-l.x;
  ui_text_line(l.x,bottom-8,hint_width,hint,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }
}
void ui_theme_home_footer(const char *path,int bottom){
 ui_theme_home_footer_with_focus(path,bottom,0);
}
UiThemeHomeNav ui_theme_home_navigation(int theme,int focused,int count,unsigned nav,unsigned rail_nav){
 UiThemeHomeNav result={rail_nav,nav,focused};
 if(theme!=VT_UI_THEME_DECK_90)return result;
 result.tabs=0;
 if(focused){result.tabs=((nav&SCE_CTRL_UP)?SCE_CTRL_LEFT:0)|((nav&SCE_CTRL_DOWN)?SCE_CTRL_RIGHT:0);
  if(count>0&&(nav&SCE_CTRL_RIGHT))result.focused=0;
  result.content=0;
 }else if(nav&SCE_CTRL_LEFT){result.focused=1;result.content&=~SCE_CTRL_LEFT;}
 return result;
}
