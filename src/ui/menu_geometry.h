#ifndef VMD_UI_MENU_GEOMETRY_H
#define VMD_UI_MENU_GEOMETRY_H
#include <math.h>
/* Category headings occupy their own 34-pixel slots, outside selectable rows. */
#define UI_MENU_HEADING_HEIGHT 34.f
static inline int ui_menu_group_start(unsigned starts,int row){return row>=0&&row<32&&((starts>>row)&1u);}
static inline float ui_menu_group_y(int row,float origin,float step,unsigned starts){
 if(row<0)row=0;
 unsigned mask=row>=31?~0u:(1u<<(row+1))-1u;
 return origin+row*step+__builtin_popcount(starts&mask)*UI_MENU_HEADING_HEIGHT;
}
static inline float ui_menu_group_focus_y(float row,float origin,float step,unsigned starts){
 if(row<0)row=0;
 int first=(int)floorf(row);float f=row-first;
 return ui_menu_group_y(first,origin,step,starts)*(1-f)+ui_menu_group_y(first+1,origin,step,starts)*f;
}
#endif
