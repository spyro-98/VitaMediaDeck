#ifndef VMD_UI_NAVIGATION_H
#define VMD_UI_NAVIGATION_H
#include <psp2/ctrl.h>
#include "ui/spatial_focus.h"
static inline int ui_cardinal_direction(unsigned nav){
    return nav&SCE_CTRL_UP?UI_FOCUS_UP:nav&SCE_CTRL_DOWN?UI_FOCUS_DOWN:
           nav&SCE_CTRL_LEFT?UI_FOCUS_LEFT:nav&SCE_CTRL_RIGHT?UI_FOCUS_RIGHT:-1;
}
/* A partial last row still has a downward neighbour. Horizontal movement
 * stays in its visual row instead of wrapping into the next one. */
static inline int ui_grid_navigation(int selected,int count,int columns,unsigned nav){
    if(count<=0)return 0;
    if(columns<1)columns=1;
    if(selected<0)selected=0;
    if(selected>=count)selected=count-1;
    switch(ui_cardinal_direction(nav)){
    case UI_FOCUS_LEFT:if(selected%columns)selected--;break;
    case UI_FOCUS_RIGHT:if(selected%columns+1<columns&&selected+1<count)selected++;break;
    case UI_FOCUS_UP:if(selected>=columns)selected-=columns;break;
    case UI_FOCUS_DOWN:
        if((selected/columns+1)*columns<count){selected+=columns;if(selected>=count)selected=count-1;}
        break;
    }
    return selected;
}
#endif
