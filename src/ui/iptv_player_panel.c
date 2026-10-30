#include "ui/iptv_player_panel.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "ui/font.h"
#include "i18n/i18n.h"
#include <time.h>
#ifdef __vita__
#include <psp2/rtc.h>
#endif
#include <psp2/kernel/processmgr.h>
#define IPTV_TR(s) vt_i18n_str(VT_STR_##s)
#include "ui/program_time.h"
static void time_range(int64_t start,int64_t end,char out[96]){
 out[0]=0;if(start<=0)return;
 time_t a=start,b=end,now=time(NULL);struct tm ta,tb,tn;
 ui_program_local_time(a,&ta);ui_program_local_time(now,&tn);
 char prefix[32]="";if(ta.tm_year!=tn.tm_year||ta.tm_mon!=tn.tm_mon||ta.tm_mday!=tn.tm_mday)snprintf(prefix,sizeof(prefix),"%02d/%02d · ",ta.tm_mday,ta.tm_mon+1);
 if(end>start){ui_program_local_time(b,&tb);snprintf(out,96,"%s%02d:%02d – %02d:%02d",prefix,ta.tm_hour,ta.tm_min,tb.tm_hour,tb.tm_min);}
 else snprintf(out,96,"%s%02d:%02d",prefix,ta.tm_hour,ta.tm_min);
}
static void program_append(char body[12288],const char *value){
 if(!value||!*value)return;
 size_t used=strlen(body);snprintf(body+used,12288-used,"%s%s",used?"\n":"",value);
}
static void program_draw(UiIptvPlayerPanel *p,float x,const VtIptvOptions *o){
 char body[12288]="",range[96];VtIptvProgram *g=&p->program;
 if(p->program_loading)program_append(body,IPTV_TR(IPTV_PROGRAM_LOADING));
 if(g->available&&g->end>time(NULL)){
  program_append(body,IPTV_TR(IPTV_ON_AIR));program_append(body,g->title);
  time_range(g->start,g->end,range);program_append(body,range);
  int64_t now=time(NULL);
  if(g->end>now&&g->end-now<86400){
   time_t end=g->end;struct tm tm;ui_program_local_time(end,&tm);char clock[16],remaining[160];
   snprintf(clock,sizeof(clock),"%02d:%02d",tm.tm_hour,tm.tm_min);
   snprintf(remaining,sizeof(remaining),IPTV_TR(IPTV_ENDS_AT),clock,(int)((g->end-now+59)/60));program_append(body,remaining);
  }
  program_append(body,g->subtitle);program_append(body,g->category);program_append(body,g->episode);program_append(body,g->description);
 }else if(!p->program_loading){
  const char *reason=p->program_error?IPTV_TR(IPTV_PROGRAM_ERROR):g->guide_status==VT_IPTV_GUIDE_MISSING?IPTV_TR(IPTV_GUIDE_MISSING):g->guide_status==VT_IPTV_GUIDE_NO_CHANNEL?IPTV_TR(IPTV_GUIDE_NO_CHANNEL):IPTV_TR(IPTV_GUIDE_NO_PROGRAM);
  program_append(body,reason);
 }
 if(g->upcoming_count){
  program_append(body," ");program_append(body,IPTV_TR(IPTV_NEXT_PROGRAM));
  for(unsigned i=0;i<g->upcoming_count&&i<VT_IPTV_UPCOMING_MAX;i++){
   VtIptvUpcoming *next=&g->upcoming[i];time_range(next->start,next->end,range);
   program_append(body,range);program_append(body,next->title);program_append(body,next->description);program_append(body," ");
  }
 }else if(g->next_title[0]){
  program_append(body,IPTV_TR(IPTV_NEXT_PROGRAM));time_range(g->next_start,g->next_end,range);
  program_append(body,range);program_append(body,g->next_title);
 }
 if(g->stale||(p->program_error&&g->available))program_append(body,IPTV_TR(IPTV_GUIDE_STALE));
 program_append(body,o?o->title:NULL);program_append(body,o?o->provider:NULL);

 vita2d_font *font=ui_runtime_font(UI_FONT_SMALL);const char *text=body;int row=0;
 while(*text&&row<350){
  char line[512];size_t n=0,fit=0,space=0;
  if(*text=='\n'){text++;row++;continue;}
  while(text[n]&&text[n]!='\n'&&n<sizeof(line)-5){size_t end=n+1;while(text[end]&&((unsigned char)text[end]&0xc0)==0x80)end++;
   if(end>=sizeof(line))break;
   memcpy(line,text,end);line[end]=0;if(ui_font_text_width(font,UI_FONT_SMALL,line)>366)break;
   if(text[n]==' ')space=n;
   fit=end;n=end;
  }
  if(!fit)break;
  if(text[n]&&text[n]!='\n'&&space)fit=space;
  memcpy(line,text,fit);line[fit]=0;
  if(94+row*24-p->viewport.position>=60&&94+row*24-p->viewport.position<498){ui_menu_text(x+28,94+row*24-p->viewport.position,366,line,UI_MENU_TEXT,UI_FONT_SMALL);}
  text+=fit;while(*text==' ')text++;if(*text=='\n')text++;row++;
 }
 int max=row>16?row-16:0;if(p->scroll>max)p->scroll=max;ui_scroll_bounds(&p->viewport,row*24,384);
 ui_scroll_rail(x+416,76,384,p->viewport.position,384,row*24);
}
void ui_iptv_player_panel_draw(UiIptvPlayerPanel *p,float x,const VtIptvOptions *o,const VtIptvSelection *selection){
 /* Redraw the header in the drawer's own surface before selecting its title. */
 
 ui_menu_text(x+28,47,366,p->page==1?IPTV_TR(IPTV_QUALITY):p->page==2?IPTV_TR(IPTV_AUDIO_LANGUAGE):IPTV_TR(IPTV_PROGRAM),UI_MENU_TEXT,UI_FONT_BODY);
 if(p->page==3){

  ui_scroll_tick(&p->viewport,sceKernelGetProcessTimeWide(),vt_preferences_reduce_motion());
  vita2d_set_clip_rectangle((int)x,76,960,478);vita2d_enable_clipping();program_draw(p,x,o);vita2d_disable_clipping();
 }
 else {
  int map[VT_IPTV_CHOICE_MAX+2];int count=ui_iptv_choices(o,selection,p->page==2,map);
  if(p->cursor>=count)p->cursor=count-1;
  if(p->cursor<0)p->cursor=0;
  ui_scroll_bounds(&p->viewport,count*56-6,392);
  if(!p->viewport.ready)ui_scroll_reveal(&p->viewport,p->cursor*56,p->cursor*56+50,392);
  ui_scroll_tick(&p->viewport,sceKernelGetProcessTimeWide(),vt_preferences_reduce_motion());
  p->top=(int)floorf(p->viewport.position/56);
  vita2d_set_clip_rectangle((int)x,76,960,468);vita2d_enable_clipping();
  ui_focus_motion_tick(&p->focus,x+18,76+p->cursor*56-p->viewport.position,394,50);
  ui_menu_focus_draw(&p->focus);
  for(int index=p->top;index<count&&76+index*56-p->viewport.position<468;index++){
   int idx=map[index];const VtIptvChoice *c=idx<0?NULL:p->page==2?&o->audio[idx]:&o->video[idx];
   char label[256],detail[256];
   if(idx== -2){snprintf(label,sizeof(label),"%s",IPTV_TR(IPTV_RECALIBRATE));snprintf(detail,sizeof(detail),"%s",IPTV_TR(IPTV_MEASURE_VARIANTS));}
   else if(!c){snprintf(label,sizeof(label),"%s",p->page==2?IPTV_TR(IPTV_SOURCE_DEFAULT):IPTV_TR(IPTV_AUTOMATIC));snprintf(detail,sizeof(detail),"%s",count==1?IPTV_TR(IPTV_SOURCE_ONLY):"");}
   else if(p->page==1){if(c->width&&c->height)snprintf(label,sizeof(label),"%up · %u × %u",c->height,c->width,c->height);else snprintf(label,sizeof(label),"%s",c->name[0]?c->name:c->codec[0]?c->codec:IPTV_TR(IPTV_QUALITY));
    snprintf(detail,sizeof(detail),"%s%s%.2f Mbps %s",c->supported?"":IPTV_TR(IPTV_CODEC_UNSUPPORTED),c->supported?"":" · ",c->bandwidth/1000000.0,c->codec);
   }else {snprintf(label,sizeof(label),"%s%s%s",c->language,c->language[0]&&c->name[0]?" · ":"",c->name);
    if(!label[0])snprintf(label,sizeof(label),"%s %d",IPTV_TR(PLAYER_AUDIO_TRACK),idx+1);
    snprintf(detail,sizeof(detail),"%s",c->supported?c->codec:IPTV_TR(IPTV_CODEC_UNSUPPORTED));}
   int y=(int)lroundf(76+index*56-p->viewport.position);ui_menu_row(x+18,y,394,56,p->cursor==index,1);
   const char *active=selection?(p->page==2?selection->audio:selection->video):"";
   int selected=idx!= -2&&(p->page==1&&!c?(!selection||!selection->video_manual):!strcmp(active,c?c->id:""));
   if(selected)ui_menu_text(x+330,y+21,70,IPTV_TR(IPTV_SELECTED),UI_MENU_ACCENT,UI_FONT_SMALL);
   ui_menu_text(x+30,y+21,selected?286:370,label,c&&!c->supported?UI_MENU_MUTED:UI_MENU_TEXT,UI_FONT_SMALL);
   ui_menu_text(x+30,y+42,370,detail,UI_MENU_MUTED,UI_FONT_SMALL);
  }
  vita2d_disable_clipping();
  ui_scroll_rail(x+416,76,386,(int)p->viewport.position,392,(int)p->viewport.maximum+392);
 }
 ui_text_block(x+28,494,374,24,2,p->page==3?IPTV_TR(IPTV_PROGRAM_HINT):IPTV_TR(IPTV_OPTIONS_HINT),UI_MENU_MUTED,UI_FONT_SMALL);
}
