/* Included by the IPTV browser: the catalogue remains the single navigation
 * source. Guide requests own value snapshots and never block this render loop. */
#include "media/iptv_program_async.h"
#include "ui/program_time.h"
#define GUIDE_ROWS 6
#define GUIDE_CACHE 24
typedef struct {
 uint64_t key,used,fetched;
 int error;
 VtIptvProgram program;
} GuideGridCache;
static GuideGridCache guide_grid_cache[GUIDE_CACHE];
static GuideGridCache *grid_cached(const VtIptvChannel *channel,uint64_t now){
 uint64_t key=ui_page_key(channel->url,channel->guide_url)^ui_page_key(channel->channel_id,channel->guide_name);
 GuideGridCache *old=&guide_grid_cache[0];
 for(int i=0;i<GUIDE_CACHE;i++){
  if(guide_grid_cache[i].key==key){guide_grid_cache[i].used=now;return &guide_grid_cache[i];}
  if(guide_grid_cache[i].used<old->used)old=&guide_grid_cache[i];
 }
 memset(old,0,sizeof(*old));old->key=key;old->used=now;return old;
}
static void grid_clock(int64_t when,char out[32]){
 struct tm tm;ui_program_local_time((time_t)when,&tm);
 snprintf(out,32,"%02d:%02d",tm.tm_hour,tm.tm_min);
}
static int grid_event(const VtIptvProgram *p,unsigned index,int64_t *start,int64_t *end,
                     const char **title,const char **description){
 if(index==0){
  if(!p->available)return 0;
  *start=p->start;*end=p->end;*title=p->title;*description=p->description;
 }else{
  if(index-1>=p->upcoming_count)return 0;
  const VtIptvUpcoming *v=&p->upcoming[index-1];
  *start=v->start;*end=v->end;*title=v->title;*description=v->description;
 }
 return *end>*start;
}
static int show_guide_grid(void){
 unsigned channels[VT_IPTV_CHANNEL_MAX],count=0;int selected=0;
 for(unsigned i=0;i<result_count;i++)if(!vt_iptv_channel_collection(&catalog.items[results[i]])){
  if((int)i==catalog_focus)selected=(int)count;
  channels[count++]=results[i];
 }
 if(!count)return -1;
 const float column_width=216,plot_x=76,plot_width=864,plot_y=146,plot_height=280,seconds_per_pixel=24;
 UiScrollView time_scroll={0},channel_scroll={0};UiFocusMotion focus={0};UiNavRepeat repeat={0};UiContextMenu options={0};
 SceCtrlData previous={0},controls={0};sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();
 int64_t now_time=time(NULL),base=now_time-now_time%1800-3600,anchor=now_time;
 uint64_t previous_tick=0,next_schedule=0;int last_first=-1,result=-1,horizontal_drag=0,follow=1;
 VtIptvProgramJob *job=NULL;unsigned job_indices[GUIDE_ROWS],job_count=0,received=0;
 ProgramContext *contexts=calloc(GUIDE_ROWS,sizeof(*contexts));VtIptvProgram *completed=calloc(1,sizeof(*completed));
 if(!contexts||!completed){free(contexts);free(completed);return -1;}
 ui_scroll_bounds(&channel_scroll,count*column_width,plot_width);
 ui_scroll_reveal(&channel_scroll,selected*column_width,(selected+1)*column_width,plot_width);
 ui_scroll_tick(&channel_scroll,sceKernelGetProcessTimeWide(),1);
 for(;;){
  uint64_t tick=sceKernelGetProcessTimeWide();float dt=previous_tick?(tick-previous_tick)/1000000.f:1.f/60;
  previous_tick=tick;if(dt>.1f)dt=.1f;now_time=time(NULL);
  sceCtrlPeekBufferPositive(0,&controls,1);unsigned pressed=controls.buttons&~previous.buttons;previous=controls;
  UiTouchEvent touch;unsigned flags=ui_touch_poll(&touch);
  UiContextAction actions[]={ {TR(IPTV_GRID_NOW),NULL,0,1},{TR(UI_REFRESH),NULL,1,1},{TR(UI_BACK),NULL,2,1} };
  int option=ui_context_menu_input(&options,actions,3,&pressed,controls.buttons,controls.ly,&flags,&touch);
  ui_context_menu_tick(&options);
  if(option==0)pressed|=SCE_CTRL_TRIANGLE;
  if(option==1)pressed|=SCE_CTRL_SQUARE;
  if(option==2)break;
  int modal=options.open||options.animation>.01f;
  unsigned nav=ui_nav_repeat_update(&repeat,pressed,controls.buttons,128,128,SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
  if(!modal&&(pressed&SCE_CTRL_CIRCLE))break;
  if(modal){nav=0;flags=0;}
  ui_scroll_bounds(&time_scroll,10*3600/seconds_per_pixel,plot_height);
  ui_scroll_bounds(&channel_scroll,count*column_width,plot_width);
  if((nav&SCE_CTRL_LEFT)&&selected>0)selected--;
  if((nav&SCE_CTRL_RIGHT)&&selected+1<(int)count)selected++;
  if(nav&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))ui_scroll_reveal(&channel_scroll,selected*column_width,(selected+1)*column_width,plot_width);
  if(nav&SCE_CTRL_UP){follow=0;anchor-=1800;time_scroll.target-=1800/seconds_per_pixel;time_scroll.coasting=0;}
  if(nav&SCE_CTRL_DOWN){follow=0;anchor+=1800;time_scroll.target+=1800/seconds_per_pixel;time_scroll.coasting=0;}
  int now_action=(pressed&SCE_CTRL_TRIANGLE)||((flags&UI_TOUCH_EVENT_TAP)&&ui_touch_hit_rect(touch.x,touch.y,748,68,192,44));
  if(now_action){base=now_time-now_time%1800-3600;anchor=now_time;follow=1;time_scroll.coasting=time_scroll.dragging=0;}
  int inside=touch.down_x>=plot_x&&touch.down_x<plot_x+plot_width&&touch.down_y>=plot_y&&touch.down_y<plot_y+plot_height;
  if((flags&UI_TOUCH_EVENT_DOWN)&&inside){
   horizontal_drag=0;ui_scroll_drag_begin(&time_scroll,touch.y);channel_scroll.down_position=channel_scroll.position;
  }
  if(time_scroll.dragging&&(flags&(UI_TOUCH_EVENT_MOVE|UI_TOUCH_EVENT_HOLD|UI_TOUCH_EVENT_UP))){
   if(abs(touch.x-touch.down_x)>abs(touch.y-touch.down_y)&&abs(touch.x-touch.down_x)>10)horizontal_drag=1;
   if(horizontal_drag){time_scroll.dragging=0;ui_scroll_drag_begin(&channel_scroll,touch.down_x);}
   else if(abs(touch.y-touch.down_y)>8||!follow){
    follow=0;ui_scroll_drag_move(&time_scroll,touch.down_y,touch.y,dt);
    if(flags&UI_TOUCH_EVENT_UP)ui_scroll_drag_end(&time_scroll,vt_preferences_reduce_motion());
   }else if(flags&UI_TOUCH_EVENT_UP)time_scroll.dragging=0;
  }
  if(channel_scroll.dragging&&(flags&(UI_TOUCH_EVENT_MOVE|UI_TOUCH_EVENT_HOLD|UI_TOUCH_EVENT_UP))){
   ui_scroll_drag_move(&channel_scroll,touch.down_x,touch.x,dt);
   if(flags&UI_TOUCH_EVENT_UP)ui_scroll_drag_end(&channel_scroll,vt_preferences_reduce_motion());
  }
  int time_stick=modal?0:ui_scroll_stick(&time_scroll,controls.ly,dt),channel_stick=modal?0:ui_scroll_stick(&channel_scroll,controls.lx,dt);
  if(time_stick)follow=0;
  if(follow){
   if(now_time-base>8*3600){base+=3600;}
   float wanted=(now_time-base)/seconds_per_pixel-100;
   time_scroll.position=time_scroll.target=ui_scroll_clamp(wanted,time_scroll.maximum);time_scroll.velocity=0;anchor=now_time;
  }
  ui_scroll_tick(&time_scroll,tick,vt_preferences_reduce_motion());ui_scroll_tick(&channel_scroll,tick,vt_preferences_reduce_motion());
  int first=(int)floorf(channel_scroll.position/column_width);
  if(channel_stick||channel_scroll.dragging||channel_scroll.coasting){
   int last=(int)floorf((channel_scroll.position+plot_width)/column_width)-1;
   if(selected<first)selected=first;
   if(selected>last)selected=last;
   if(selected>=(int)count)selected=(int)count-1;
  }
  if(!follow&&(time_stick||time_scroll.dragging||time_scroll.coasting))anchor=base+(int64_t)((time_scroll.position+100)*seconds_per_pixel);
  if((flags&UI_TOUCH_EVENT_TAP)&&inside){
   selected=(int)((touch.x-plot_x+channel_scroll.position)/column_width);
   if(selected>=(int)count)selected=(int)count-1;
   anchor=base+(int64_t)((time_scroll.position+touch.y-plot_y)*seconds_per_pixel);follow=0;
  }
  if(!modal&&(pressed&SCE_CTRL_CROSS)){result=(int)channels[selected];break;}
  if(job)for(unsigned i=0;i<job_count;i++)if(!(received&(1u<<i))){
   int ready=vt_iptv_program_poll_row(job,i,completed);
   if(ready){GuideGridCache *cache=grid_cached(&catalog.items[job_indices[i]],tick);
    if(ready>0)cache->program=*completed;
    cache->error=ready<0?ready:0;cache->fetched=tick;received|=1u<<i;}
  }
  if(pressed&SCE_CTRL_SQUARE){for(unsigned i=0;i<GUIDE_CACHE;i++)guide_grid_cache[i].fetched=0;last_first=-1;}
  if(first!=last_first||(job&&received==((1u<<job_count)-1))||(!job&&tick>=next_schedule)){
   vt_iptv_program_release(job);job=NULL;last_first=first;job_count=received=0;next_schedule=tick+1000000;
   for(int r=-1;r<GUIDE_ROWS&&job_count<GUIDE_ROWS;r++){
    int index=r<0?selected:first+r;if(index<0||index>=(int)count)continue;
    unsigned channel=channels[index];int duplicate=0;for(unsigned i=0;i<job_count;i++)if(job_indices[i]==channel)duplicate=1;
    GuideGridCache *cache=grid_cached(&catalog.items[channel],tick);
    if(duplicate||(cache->fetched&&tick-cache->fetched<(cache->error?30000000ULL:300000000ULL)))continue;
    ProgramContext *ctx=&contexts[job_count];memset(ctx,0,sizeof(*ctx));ctx->channel=catalog.items[channel];
    if(!saved_view)snprintf(ctx->playlist,sizeof(ctx->playlist),"%s",current_list.url);
    snprintf(ctx->language,sizeof(ctx->language),"%s",vt_i18n_language_code());job_indices[job_count++]=channel;
   }
   if(job_count)job=vt_iptv_program_request_batch(load_player_program,contexts,sizeof(*contexts),job_count,0);
  }
  vita2d_start_drawing();vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
  ui_brand_draw_breadcrumbs(TR(IPTV_TITLE),current_list.title,TR(IPTV_GRID_TITLE));
  text_line(24,84,700,TR(IPTV_GRID_TITLE),VT_THEME_TEXT,UI_FONT_DISPLAY);
  ui_action_button(748,68,192,44,VT_THEME_SURFACE,UI_PAD_TRIANGLE,TR(IPTV_GRID_NOW),0);
  text_line(424,84,300,follow?TR(IPTV_GRID_FOLLOW):TR(IPTV_GRID_MANUAL),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  int64_t window=base+(int64_t)(time_scroll.position*seconds_per_pixel);
  const char *detail_title="",*detail_description="";int64_t detail_start=0,detail_end=0;
  vita2d_set_clip_rectangle(18,plot_y,940,plot_y+plot_height);vita2d_enable_clipping();
  int64_t first_half_hour=window-window%1800;
  for(int64_t when=first_half_hour;when<window+(int64_t)(plot_height*seconds_per_pixel)+1800;when+=1800){
   float y=plot_y+(when-window)/seconds_per_pixel;char clock[32];grid_clock(when,clock);
   if(y>=plot_y+12&&y<plot_y+plot_height-8)text_line(20,y+8,54,clock,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
   vita2d_draw_line(plot_x,y,plot_x+plot_width,y,VT_THEME_BORDER_DIM);
  }
  int any_guide=0,focus_ready=0;
  for(int r=first;r<(int)count&&r<first+GUIDE_ROWS;r++){
   float x=plot_x+r*column_width-channel_scroll.position,left=fmaxf(plot_x,x),right=fminf(plot_x+plot_width,x+column_width);
   if(right<=left)continue;
   VtIptvChannel *channel=&catalog.items[channels[r]];GuideGridCache *cache=grid_cached(channel,tick);int events=0;
   vita2d_draw_line(x,plot_y,x,plot_y+plot_height,VT_THEME_BORDER);
   for(unsigned e=0;e<=VT_IPTV_UPCOMING_MAX;e++){
    int64_t begin,end;const char *title,*description;if(!grid_event(&cache->program,e,&begin,&end,&title,&description))continue;
    any_guide=1;int chosen=r==selected&&anchor>=begin&&anchor<end;
    if(chosen){detail_title=title;detail_description=description;detail_start=begin;detail_end=end;}
    float top=plot_y+(begin-window)/seconds_per_pixel,bottom=plot_y+(end-window)/seconds_per_pixel;
    if(bottom<=plot_y||top>=plot_y+plot_height)continue;
    events++;
    top=fmaxf(plot_y,top);bottom=fminf(plot_y+plot_height,bottom);
    if(chosen){ui_focus_motion_tick(&focus,left+2,top+2,right-left-4,bottom-top-4);focus_ready=1;}
    vita2d_draw_line(left+3,bottom-1,right-3,bottom-1,VT_THEME_BORDER);
    if(bottom-top>22)text_line(left+8,top+21,right-left-16,title,VT_THEME_TEXT,UI_FONT_SMALL);
    if(bottom-top>48){char a[32],b[32],range[80];grid_clock(begin,a);grid_clock(end,b);snprintf(range,sizeof(range),"%s–%s",a,b);
     text_line(left+8,top+45,right-left-16,range,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);}
   }
   if(!events)text_line(left+8,plot_y+46,right-left-16,cache->fetched?TR(IPTV_GRID_UNAVAILABLE):TR(IPTV_PROGRAM_LOADING),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  }
  if(focus_ready)ui_focus_outline_draw(&focus);
  float now_y=plot_y+(now_time-window)/seconds_per_pixel;
  if(any_guide&&now_y>=plot_y&&now_y<plot_y+plot_height){
   unsigned red=RGBA8(255,72,82,255);vita2d_draw_rectangle(plot_x,now_y,plot_width,2,red);
   vita2d_draw_rectangle(plot_x-3,now_y-2,6,6,red);
   char clock[32];grid_clock(now_time,clock);vita2d_draw_rectangle(18,now_y-12,56,24,VT_THEME_BG_SOFT);
   text_line(20,now_y+7,54,clock,red,UI_FONT_SMALL);
  }
  vita2d_disable_clipping();
  vita2d_set_clip_rectangle(plot_x,112,940,plot_y-2);vita2d_enable_clipping();
  for(int r=first;r<(int)count&&r<first+GUIDE_ROWS;r++){
   float x=plot_x+r*column_width-channel_scroll.position;
   text_line(x+8,135,column_width-16,catalog.items[channels[r]].title,r==selected?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT,UI_FONT_SMALL);
  }
  vita2d_disable_clipping();
  if(detail_title[0]){char a[32],b[32],heading[384];grid_clock(detail_start,a);grid_clock(detail_end,b);
   snprintf(heading,sizeof(heading),"%s–%s · %s",a,b,detail_title);text_line(24,451,912,heading,VT_THEME_TEXT,UI_FONT_SMALL);
   text_line(24,478,912,detail_description,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);}
  ui_text_block(24,506,912,24,2,TR(IPTV_GRID_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  ui_context_menu_draw(&options,TR(IPTV_GRID_TITLE),actions,3);
  vita2d_end_drawing();vita2d_wait_rendering_done();vita2d_swap_buffers();ui_frame_pace(tick);
 }
 vt_iptv_program_release(job);free(contexts);free(completed);vita2d_wait_rendering_done();ui_touch_reset();return result;
}
