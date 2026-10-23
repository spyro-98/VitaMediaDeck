#include "ui/theme_home.h"
#include "ui/theme_scene.h"
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
 if(theme==VT_UI_THEME_DECK_90)return ui_theme_scene_reveal(UI_SCENE_HOME,(UiThemeRect){28,112+p*54,188,44},p);
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
 UiThemeRect r={l.x+(slot%l.columns)*(l.w+12),l.y+(slot/l.columns)*l.step,l.w,l.step-8};
 if(theme==VT_UI_THEME_RELAY){
  r.x=264+(slot%2)*326;r.y=168+(slot/2)*128+(slot%2)*28;r.h=96;
 }else if(theme==VT_UI_THEME_MOSAIC){
  r.y=210+abs(slot%5-2)*20;
  r.h=bottom-38-250;if(r.h>244)r.h=244;if(r.h<110)r.h=110;
 }
 return ui_theme_scene_reveal(UI_SCENE_HOME,r,slot);
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
void ui_theme_fx_background(unsigned base){
 int theme=ui_theme_id();if(theme==VT_UI_THEME_LEGACY)return;
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
 int theme=ui_theme_id();if(theme==VT_UI_THEME_LEGACY||w<60||h<30)return;
 fx_shadow(x,y,w,h,theme==VT_UI_THEME_SIGNAL);
 unsigned tint=theme==VT_UI_THEME_DECK_90?VT_THEME_SIGNAL_BRIGHT:VT_THEME_COLD;
 for(int i=0;i<5;i++)vita2d_draw_rectangle(x,y+i*(h/5),w,h/5+1,
     fx_mix(fill,tint,(unsigned)((selected?22:10)*(5-i)/5)));
 vita2d_draw_line(x+7,y,x+w-10,y,fx_alpha(VT_THEME_TEXT,selected?100:42));
 vita2d_draw_line(x+w-1,y+8,x+w-1,y+h-5,RGBA8(0,0,0,80));
 if(theme==VT_UI_THEME_DECK_90){
  vita2d_draw_rectangle(x+2,y+2,2,h-4,RGBA8(0,0,0,100));
  vita2d_draw_rectangle(x+5,y+h-3,w-10,1,fx_alpha(tint,60));
 }else{
  /* Faceted specular lip: a narrow reflection, independent of text/artwork. */
  for(int i=0;i<4;i++)vita2d_draw_rectangle(x+8+i*3,y+2+i,w-24-i*6,1,
      fx_alpha(VT_THEME_TEXT,(unsigned)(selected?38-i*7:16-i*3)));
  vita2d_draw_line(x,y+9,x+9,y,fx_alpha(tint,selected?210:75));
  vita2d_draw_line(x+w-10,y+h-1,x+w-1,y+h-10,fx_alpha(tint,90));
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
/* Actual 3D model space, perspective projection and GPU triangle batches.
 * The CPU transforms a bounded mesh; GXM rasterizes coloured triangle lists. */
typedef struct {float x,y,z;} FxVec3;
#define FX_BATCH_CAPACITY 4096
static vita2d_color_vertex fx_mesh_vertices[FX_BATCH_CAPACITY];
static unsigned fx_mesh_count;
static void fx_mesh_flush(void) {
 if(fx_mesh_count){
#ifdef __vita__
  /* draw_array retains its pointer: use mapped frame-pool memory, never the
   * CPU scratch array that the next batch immediately overwrites. */
  unsigned bytes=fx_mesh_count*sizeof(vita2d_color_vertex);
  vita2d_color_vertex *gpu=vita2d_pool_memalign(bytes,16);
  if(gpu){memcpy(gpu,fx_mesh_vertices,bytes);vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,gpu,fx_mesh_count);}
#else
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,fx_mesh_vertices,fx_mesh_count);
#endif
 }
 fx_mesh_count=0;
}
static FxVec3 fx_rotate(FxVec3 v,float sx,float cx,float sy,float cy,float sz,float cz) {
 FxVec3 a={v.x,v.y*cx-v.z*sx,v.y*sx+v.z*cx};
 FxVec3 b={a.x*cy+a.z*sy,a.y,-a.x*sy+a.z*cy};
 return (FxVec3){b.x*cz-b.y*sz,b.x*sz+b.y*cz,b.z};
}
static vita2d_color_vertex fx_project(FxVec3 v,unsigned color) {
 float perspective=620.0f/(620.0f+v.z);
 return (vita2d_color_vertex){690+v.x*perspective,272+v.y*perspective,.5f,color};
}
static void fx_triangle(FxVec3 a,FxVec3 b,FxVec3 c,unsigned color) {
 if(fx_mesh_count+3>FX_BATCH_CAPACITY)fx_mesh_flush();
 fx_mesh_vertices[fx_mesh_count++]=fx_project(a,color);
 fx_mesh_vertices[fx_mesh_count++]=fx_project(b,color);
 fx_mesh_vertices[fx_mesh_count++]=fx_project(c,color);
}
static void fx_3d_ring(float radius,float thickness,float tilt_x,float tilt_y,float turn,int pass,unsigned color) {
 static float circle[65][2];static int ready;
 if(!ready){for(int i=0;i<=64;i++){float a=i*6.28318530718f/64;circle[i][0]=cosf(a);circle[i][1]=sinf(a);}ready=1;}
 float sx=sinf(tilt_x),cx=cosf(tilt_x),sy=sinf(tilt_y),cy=cosf(tilt_y),sz=sinf(turn),cz=cosf(turn);
 for(int i=0;i<64;i++) {
  if(i%16>=13)continue;
  FxVec3 a=fx_rotate((FxVec3){circle[i][0]*radius,circle[i][1]*radius,0},sx,cx,sy,cy,sz,cz);
  FxVec3 b=fx_rotate((FxVec3){circle[i+1][0]*radius,circle[i+1][1]*radius,0},sx,cx,sy,cy,sz,cz);
  if(((a.z+b.z)>0?0:1)!=pass)continue;
  float inner=(radius-thickness)/radius;
  FxVec3 c={b.x*inner,b.y*inner,b.z*inner},d={a.x*inner,a.y*inner,a.z*inner};
  unsigned shade=fx_mix(color,VT_THEME_BG,(unsigned)(a.z+b.z>0?100:15));
  shade=fx_alpha(shade,(color>>24)&255);
  fx_triangle(a,b,c,shade);fx_triangle(a,c,d,shade);
 }
}
static void fx_icosahedron(float turn) {
 const float t=1.61803398875f,scale=29;
 static const unsigned char faces[20][3]={
  {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
  {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
 FxVec3 vertices[12]={{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},{t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}};
 float sx=sinf(.45f),cx=cosf(.45f),sy=sinf(turn),cy=cosf(turn),sz=sinf(turn*.5f),cz=cosf(turn*.5f);
 for(int i=0;i<12;i++){vertices[i].x*=scale;vertices[i].y*=scale;vertices[i].z*=scale;vertices[i]=fx_rotate(vertices[i],sx,cx,sy,cy,sz,cz);}
 struct {float depth;unsigned char face;} ordered[20];
 for(int i=0;i<20;i++) {
  float depth=0;for(int j=0;j<3;j++)depth+=vertices[faces[i][j]].z;
  int pos=i;while(pos>0&&ordered[pos-1].depth<depth){ordered[pos]=ordered[pos-1];pos--;}
  ordered[pos].depth=depth;ordered[pos].face=(unsigned char)i;
 }
 for(int i=0;i<20;i++) {
  int face=ordered[i].face;FxVec3 a=vertices[faces[face][0]],b=vertices[faces[face][1]],c=vertices[faces[face][2]];
  FxVec3 u={b.x-a.x,b.y-a.y,b.z-a.z},v={c.x-a.x,c.y-a.y,c.z-a.z};
  FxVec3 n={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
  float length=sqrtf(n.x*n.x+n.y*n.y+n.z*n.z);
  float light=(-.35f*n.x-.5f*n.y-.79f*n.z)/length;
  if(light<0)light=0;
  unsigned color=fx_mix(VT_THEME_SURFACE_RAISED,VT_THEME_COLD,(unsigned)(42+light*180));
  if(light>.8f)color=fx_mix(color,VT_THEME_SIGNAL_BRIGHT,(unsigned)((light-.8f)*650));
  fx_triangle(a,b,c,color);
 }
}
static float fx_wheel_from,fx_wheel_to;static int fx_wheel_selected=-1;
static uint64_t fx_wheel_changed;
static float fx_wheel_angle(int selected) {
 uint64_t now=sceKernelGetProcessTimeWide();
 float elapsed=now>=fx_wheel_changed?(float)(now-fx_wheel_changed)/240000.0f:1;
 if(elapsed>1)elapsed=1;
 float eased=elapsed*elapsed*(3-2*elapsed);
 float current=fx_wheel_from+(fx_wheel_to-fx_wheel_from)*eased;
 if(fx_wheel_selected!=selected) {
  float target=-1.57079632679f+selected*1.0471975512f;
  if(fx_wheel_selected<0)current=target;
  while(target-current>3.14159265359f)target-=6.28318530718f;
  while(target-current< -3.14159265359f)target+=6.28318530718f;
  fx_wheel_from=current;fx_wheel_to=target;fx_wheel_changed=now;fx_wheel_selected=selected;
 }
 return vt_preferences_reduce_motion()?fx_wheel_to:current;
}
static void fx_ghost_core(int selected) {
 float turn=fx_phase(24000000ULL)*6.28318530718f;
 float angle=fx_wheel_angle(selected);
 fx_mesh_count=0;
 /* Concentric outer guides share the same exact center as the six nodes. */
 for(int pass=0;pass<2;pass++) {
  fx_3d_ring(170,1.2f,0,0,turn,pass,fx_alpha(VT_THEME_COLD,185));
  fx_3d_ring(166,1,0,0,-turn,pass,fx_alpha(VT_THEME_SIGNAL_BRIGHT,195));
  fx_3d_ring(108,2.2f,.8f,.16f,turn,pass,fx_alpha(VT_THEME_COLD,235));
  fx_3d_ring(92,2.6f,-.45f,.9f,-turn,pass,fx_alpha(VT_THEME_SIGNAL_BRIGHT,245));
  fx_3d_ring(76,1.7f,.6f,-.8f,turn*.5f,pass,fx_alpha(VT_THEME_COLD,235));
  fx_mesh_flush();
  if(pass==0){fx_icosahedron(turn);fx_mesh_flush();}
 }
 /* A smoothly moving angular cursor, rather than snapping an entire ring. */
 for(int i=0;i<12;i++) {
  float a=angle-.12f+i*.02f,b=a+.02f;
  FxVec3 p={cosf(a)*173,sinf(a)*173,0},q={cosf(b)*173,sinf(b)*173,0};
  FxVec3 r={cosf(b)*178,sinf(b)*178,0},t={cosf(a)*178,sinf(a)*178,0};
  fx_triangle(p,q,r,VT_THEME_SIGNAL_BRIGHT);fx_triangle(p,r,t,VT_THEME_SIGNAL_BRIGHT);
 }
 fx_mesh_flush();
 vita2d_draw_line(690+cosf(angle)*58,272+sinf(angle)*58,690+cosf(angle)*149,272+sinf(angle)*149,fx_alpha(VT_THEME_SIGNAL_BRIGHT,120));
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
 fx_select(theme,selected);
 if(theme==VT_UI_THEME_GHOST_MESH){
  fx_ghost_core(selected);
  ui_text_line(625,400,130,labels[selected],VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }else if(theme==VT_UI_THEME_DECK_90){
  vita2d_draw_rectangle(234,70,1,388,VT_THEME_BORDER);
  ui_text_line(28,92,184,vt_i18n_str(VT_STR_SECTIONS_TITLE),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }else if(theme==VT_UI_THEME_APERTURE){
  vita2d_draw_rectangle(18,416,924,1,VT_THEME_BORDER);
 }else if(theme==VT_UI_THEME_RELAY){
  for(int i=1;i<6;i++){
   UiThemeRect a=ui_theme_home_tab_rect(theme,i-1,1),b=ui_theme_home_tab_rect(theme,i,1);
   vita2d_draw_line(a.x+12,a.y+a.h*.5f,b.x+12,b.y+b.h*.5f,VT_THEME_COLD);
  }
 }
 for(int i=0;i<6;i++){
  UiThemeRect r=ui_theme_home_tab_rect(theme,i,1);int active=i==selected;int category=categories?categories[i]:i;
  unsigned fg=active?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT_MUTED;
  if(theme==VT_UI_THEME_GHOST_MESH){
   ui_theme_fx_surface(r.x,r.y,r.w,r.h,active?fx_mix(VT_THEME_SURFACE,VT_THEME_COLD,48):VT_THEME_SURFACE,active);
   if(active){vita2d_draw_rectangle(r.x-5,r.y+8,3,r.h-16,VT_THEME_SIGNAL_BRIGHT);vita2d_draw_rectangle(r.x+9,r.y-3,r.w*.55f,2,VT_THEME_COLD);}
   float dx=r.x+r.w*.5f-690,dy=r.y+r.h*.5f-272,len=sqrtf(dx*dx+dy*dy);
   vita2d_draw_line(690+dx*72/len,272+dy*72/len,r.x+r.w*.5f,r.y+r.h*.5f,active?VT_THEME_FOCUS:VT_THEME_BORDER_DIM);
  }
  if(theme==VT_UI_THEME_DECK_90&&active&&focused){
   vita2d_draw_rectangle(r.x,r.y,r.w,r.h,VT_THEME_SIGNAL_BRIGHT);fg=VT_THEME_BG;
  }
  if(theme==VT_UI_THEME_RELAY||theme==VT_UI_THEME_MOSAIC){
   vita2d_draw_rectangle(r.x,r.y,r.w,r.h,active?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE);
   corners(r,active&&focused?VT_THEME_FOCUS:VT_THEME_BORDER_DIM);
   if(active)vita2d_draw_rectangle(r.x+12,r.y+12,22,3,VT_THEME_SIGNAL_BRIGHT);
  }
  if(theme==VT_UI_THEME_RELAY)ui_theme_scene_card(UI_SCENE_HOME,r,active,i);
  if(theme==VT_UI_THEME_APERTURE){
   for(int p=0;p<r.w-12;p+=18)vita2d_draw_rectangle(r.x+6+p,r.y-6,7,3,VT_THEME_BORDER);
   ui_theme_scene_transport(r.x+5,r.y+5,r.w-10,r.h-10,active,active?1:.2f);
  }
  if(theme==VT_UI_THEME_DECK_90){
   vita2d_draw_line(r.x+8,r.y+r.h-2,r.x+r.w-8,r.y+r.h-2,VT_THEME_BORDER_DIM);
   if(active)vita2d_draw_rectangle(r.x+2,r.y+4,3,r.h-8,fg);
  }
  if(theme==VT_UI_THEME_SIGNAL)ui_theme_scene_transport(r.x+5,r.y+4,r.w-10,r.h-8,active,active?.8f:.15f);
  if(theme==VT_UI_THEME_MOSAIC&&active)ui_theme_scene_transport(r.x,r.y,r.w,r.h,1,1);
  if(theme==VT_UI_THEME_SIGNAL || (theme==VT_UI_THEME_DECK_90&&active&&focused))
   ui_theme_fx_surface(r.x,r.y,r.w,r.h,theme==VT_UI_THEME_DECK_90&&active&&focused?VT_THEME_SIGNAL_BRIGHT:active?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE,active);
  int inset=12,baseline=r.y+(r.h>60?66:r.h>44?35:27);
  if(theme==VT_UI_THEME_GHOST_MESH){fx_category_icon(r.x+10,r.y+11,.8f,category,fg);inset=42;}
  if(theme==VT_UI_THEME_MOSAIC){fx_category_icon(r.x+10,r.y+13,.8f,category,fg);inset=42;}
  if(theme==VT_UI_THEME_RELAY){fx_category_icon(r.x+10,r.y+11,.8f,category,fg);inset=42;}
  ui_text_line(r.x+inset,baseline,r.w-inset-12,labels[i],fg,UI_FONT_SMALL);
  if(active&&theme!=VT_UI_THEME_DECK_90&&theme!=VT_UI_THEME_RELAY&&theme!=VT_UI_THEME_MOSAIC)
   vita2d_draw_rectangle(r.x+12,r.y+r.h-2,r.w-24,2,VT_THEME_SIGNAL_BRIGHT);
 }
 if(focused&&motion){UiThemeRect r=ui_theme_home_tab_rect(theme,selected,1);
  ui_focus_motion_tick(motion,r.x,r.y,r.w,r.h);
  if(theme==VT_UI_THEME_GHOST_MESH||theme==VT_UI_THEME_RELAY||theme==VT_UI_THEME_MOSAIC){
   UiThemeRect moving={(int)motion->x,(int)motion->y,(int)motion->width,(int)motion->height};corners(moving,VT_THEME_FOCUS);
  }
  else ui_focus_outline_draw(motion);
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
  for(int i=1;i<4;i++){
   UiThemeRect a=ui_theme_home_item_rect(theme,i-1,bottom),b=ui_theme_home_item_rect(theme,i,bottom);
   float x=a.x+a.w*.5f,y=a.y+a.h*.5f,nx=b.x+b.w*.5f,ny=b.y+b.h*.5f;
   vita2d_draw_line(x,y,nx,y,VT_THEME_COLD);vita2d_draw_line(nx,y,nx,ny,VT_THEME_COLD);
  }
 }
 if(theme==VT_UI_THEME_SIGNAL){
  /* Parallel edges and constant thickness: an intentional diagonal, no stair gaps. */
  for(int i=0;i<24;i++)vita2d_draw_rectangle(860+i,126+i,38,1,VT_THEME_SIGNAL_BRIGHT);
 }
}
static void theme_home_row(int slot,int ordinal,const char *name,const char *detail,int selected,int focused,int bottom,vita2d_texture *art,const char *placeholder){
 int theme=ui_theme_id();UiThemeRect r=ui_theme_home_item_rect(theme,slot,bottom);
 if(r.h>80){ui_theme_browser_tile(UI_SCENE_HOME,r,slot,selected&&focused,art,name,detail,!placeholder||!strcmp(placeholder,"DIR"));return;}
 unsigned fg=VT_THEME_TEXT,muted=VT_THEME_TEXT_MUTED;
 unsigned fill=selected&&focused?(theme==VT_UI_THEME_DECK_90?VT_THEME_SIGNAL_BRIGHT:theme==VT_UI_THEME_SIGNAL?VT_THEME_TEXT:VT_THEME_SURFACE_RAISED):VT_THEME_SURFACE;
 ui_theme_scene_card(UI_SCENE_HOME,r,selected&&focused,slot);
 ui_theme_fx_surface(r.x+4,r.y+3,r.w-8,r.h-6,fill,selected&&focused);
 if(selected&&focused&&(theme==VT_UI_THEME_DECK_90||theme==VT_UI_THEME_SIGNAL))fg=muted=VT_THEME_BG;
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
 if(theme==VT_UI_THEME_APERTURE){UiThemeRect r=ui_theme_home_item_rect(theme,0,bottom);y=r.y+r.h-22;}
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
void ui_theme_home_footer(const char *path,int bottom){
 UiThemeHomeLayout l=ui_theme_home_layout(ui_theme_id(),bottom);
 if(ui_theme_id()==VT_UI_THEME_GHOST_MESH){
  ui_text_line(l.x,bottom-26,l.w,vt_i18n_str(VT_STR_HOME_GHOST_WHEEL_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  ui_text_line(l.x,bottom-8,l.w,vt_i18n_str(VT_STR_HOME_GHOST_CONTENT_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  return;
 }
 int y=ui_theme_id()==VT_UI_THEME_APERTURE?408:bottom-10;
 ui_text_line(l.x,y,l.w,path?path:"",VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
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
