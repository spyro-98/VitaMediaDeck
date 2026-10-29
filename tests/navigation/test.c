#include <stdio.h>
#include <stdlib.h>
#include "ui/navigation.h"
#include "ui/iptv_layout.h"
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"Navigation failed: %s line %d\n",#x,__LINE__);exit(1);}}while(0)
static const unsigned directions[]={SCE_CTRL_UP,SCE_CTRL_DOWN,SCE_CTRL_LEFT,SCE_CTRL_RIGHT};
int main(void){
 for(int cols=1;cols<=8;cols++)for(int count=1;count<=96;count++){
  for(int start=0;start<count;start++){
   int seen[96]={0},queue[96],head=0,tail=0;seen[start]=1;queue[tail++]=start;
   while(head<tail){int at=queue[head++];for(int d=0;d<4;d++){
    int next=ui_grid_navigation(at,count,cols,directions[d]);CHECK(next>=0&&next<count);
    if(d==2||d==3)CHECK(next/cols==at/cols);
    if(d==1&&(at/cols+1)*cols<count)CHECK(next/cols==at/cols+1);
    if(d==0&&at>=cols)CHECK(next/cols==at/cols-1);
    if(!seen[next]){seen[next]=1;queue[tail++]=next;}
   }}
   CHECK(tail==count);
  }
 }
 CHECK(ui_grid_navigation(2,5,3,SCE_CTRL_DOWN)==4);
 CHECK(ui_grid_navigation(2,5,3,SCE_CTRL_RIGHT)==2);
 CHECK(ui_grid_navigation(3,5,3,SCE_CTRL_LEFT)==3);
 CHECK(ui_grid_navigation(-1,0,3,SCE_CTRL_DOWN)==0);
 for(int playlist=0;playlist<2;playlist++)for(int link=0;link<2;link++){
  int seen[IPTV_FORM_COUNT]={0},queue[IPTV_FORM_COUNT],head=0,tail=0;
  seen[0]=1;queue[tail++]=0;
  while(head<tail){int at=queue[head++];for(int d=0;d<4;d++){
   int next=iptv_form_focus(at,directions[d],playlist,link);
   CHECK(next>=0&&next<IPTV_FORM_COUNT);
   CHECK(!playlist||next!=1);CHECK(link||(next!=2&&next!=5));
   if(!seen[next]){seen[next]=1;queue[tail++]=next;}
  }}
  for(int i=0;i<IPTV_FORM_COUNT;i++)CHECK(seen[i]==((i!=1||!playlist)&&(i!=7||playlist)&&((i!=2&&i!=5)||link)));
 }
 /* The only rail controls are 4,5,7. Every cell and visible action is reachable. */
 for(int cols=1;cols<=3;cols++)for(int count=0;count<=40;count++){
  int seen[160]={0},queue[160],head=0,tail=0,start=count?8:4;seen[start]=1;queue[tail++]=start;
  while(head<tail){int id=queue[head++];for(unsigned d=0;d<4;d++){
   int selected=id>=8?id-8:0;
   int next=iptv_grid_focus(id>=8?-1:id,&selected,0,count,96,directions[d],!count,18,924,116,cols);
   if(next<0)next=8+selected;
   CHECK(next>=4&&next<8+count&&next!=6);
   if(!seen[next]){seen[next]=1;queue[tail++]=next;}
  }}
  CHECK(seen[4]&&seen[7]&&seen[5]==!!count);
  for(int i=0;i<count;i++)CHECK(seen[8+i]);
 }
 for(int cols=1;cols<=3;cols++)for(int count=1;count<130;count++)for(int i=0;i<count;i++){
  for(unsigned d=0;d<4;d++){
   int selected=i;int top=(i/cols)*cols;
   int next=iptv_grid_focus(-1,&selected,top,count,96,directions[d],0,cols==2?340:18,cols==2?602:924,116,cols);
   CHECK(selected>=0&&selected<count);
   if(d!=0||i>=cols){CHECK(next==-1);CHECK(selected==ui_grid_navigation(i,count,cols,directions[d]));}
   else CHECK(next==4||next==5||next==7);
  }
 }
 printf("Spatial navigation: %u checks; all grid cells, partial rows and the single IPTV Add/Search/Options rail reachable with arrows; no obsolete action-row IDs.\n",checks);
}
