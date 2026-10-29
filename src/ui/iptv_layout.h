#ifndef VMD_IPTV_LAYOUT_H
#define VMD_IPTV_LAYOUT_H
#include "ui/navigation.h"

enum {IPTV_FORM_COUNT=8};
static const int form_rects[IPTV_FORM_COUNT][4]={
    {18,110,924,90},{18,352,924,48},{18,218,596,52},
    {484,286,458,52},{18,286,458,52},{630,218,312,52},{756,62,186,40},{18,352,924,48}
};
static const int form_actions[IPTV_FORM_COUNT]={0,6,7,5,3,1,9,27};
static inline int iptv_direction(unsigned nav){
    return ui_cardinal_direction(nav);
}
static inline int iptv_form_focus(int current,unsigned nav,int playlist,int has_link){
    UiFocusNode nodes[IPTV_FORM_COUNT];
    for(int i=0;i<IPTV_FORM_COUNT;i++){
        const int*r=form_rects[i];
        nodes[i]=(UiFocusNode){r[0],r[1],r[2],r[3],(i!=1||!playlist)&&(i!=7||playlist),i};
        if((i==2||i==5)&&!has_link)nodes[i].enabled=0;
    }
    int direction=iptv_direction(nav);
    return direction<0?current:ui_spatial_focus(nodes,IPTV_FORM_COUNT,current,direction);
}
/* One action rail only: Add(4), Search(5), Options(7), then real cells(8+).
 * Touch and drawing use these same rectangles. No hidden second action row. */
static const int iptv_header_rects[3][4]={{18,62,240,40},{272,62,468,40},{756,62,186,40}};
static const int iptv_header_ids[3]={4,5,7};
static inline int iptv_grid_focus(int focus,int *selected,int top,int count,int step,
 unsigned nav,int empty,int x,int width,int y,int columns){
 int d=iptv_direction(nav);if(columns<1)columns=1;
 if(focus<0&&count&&d>=0){
  if(d!=UI_FOCUS_UP||*selected>=columns){*selected=ui_grid_navigation(*selected,count,columns,nav);return -1;}
 }
 UiFocusNode nodes[32];int n=0;
 for(int i=0;i<3;i++){
  const int *r=iptv_header_rects[i];int add=empty&&!i;
  nodes[n++]=(UiFocusNode){add?68:r[0],add?306:r[1],add?824:r[2],add?52:r[3],!empty||i!=1,iptv_header_ids[i]};
 }
 int first=focus<0?*selected/columns*columns:top/columns*columns;if(first<0)first=0;
 int card_width=(width-(columns-1)*8)/columns;
 for(int i=first;i<count&&i<first+columns*3;i++)nodes[n++]=(UiFocusNode){x+(i%columns)*(card_width+8),y+(i/columns-top/columns)*step,card_width,step-8,1,8+i};
 if(d<0)return focus;
 int id=ui_spatial_focus(nodes,n,focus<0?8+*selected:focus,d);
 if(id>=8){*selected=id-8;return -1;}return id;
}
#endif
