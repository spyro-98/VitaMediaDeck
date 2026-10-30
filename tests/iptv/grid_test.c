#include <time.h>
static time_t grid_now=1793350800;
static time_t grid_now_time(time_t *p){if(p)*p=grid_now;return grid_now;}
#define time grid_now_time
#define main original_ui_main
#include "ui_test.c"
#undef main
#undef time
static int original_line,moved_line;
static int red_line(void){
 for(int y=146;y<426;y++)if(pixels[y][600][0]==255&&pixels[y][600][1]==72&&pixels[y][600][2]==82)return y;
 return -1;
}
static void grid_capture(unsigned tick){
 if(tick==0){original_line=red_line();CHECK(original_line==246);grid_now+=600;}
 if(tick==2){CHECK(red_line()==original_line);}
 if(tick==28){moved_line=red_line();CHECK(moved_line<original_line-35);}
 if(tick==32){CHECK(red_line()==original_line);char name[96];snprintf(name,sizeof(name),"epg-grid-%d-%u",ui_theme_id(),language);png(name);}
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);memset(guide_grid_cache,0,sizeof(guide_grid_cache));saved_view=1;catalog_focus=0;
  catalog.count=result_count=4;grid_now=1793350800;
  for(unsigned i=0;i<4;i++){
   results[i]=i;catalog.items[i]=(VtIptvChannel){.mode=VT_IPTV_HLS};
   snprintf(catalog.items[i].url,sizeof(catalog.items[i].url),"https://fixture.test/%u.m3u8",i);
   snprintf(catalog.items[i].title,sizeof(catalog.items[i].title),"Channel %u · long programme name",i+1);
   GuideGridCache *c=grid_cached(&catalog.items[i],1);c->fetched=1;c->program.available=i!=3;
   strcpy(c->program.title,"Science and technology");strcpy(c->program.description,"Current programme description, shared schedule times.");
   c->program.start=grid_now-1800;c->program.end=grid_now+1800;
   if(i!=3){c->program.upcoming_count=2;for(int n=0;n<2;n++){
    c->program.upcoming[n]=(VtIptvUpcoming){.start=grid_now+1800+n*3600,.end=grid_now+5400+n*3600};
    snprintf(c->program.upcoming[n].title,sizeof(c->program.upcoming[n].title),"Next programme %d",n+1);
   }}
  }
  frame=0;script_count=36;memset(input_script,0,sizeof(input_script));input_script[4]=SCE_CTRL_DOWN;
  input_script[30]=SCE_CTRL_TRIANGLE;input_script[35]=SCE_CTRL_CIRCLE;frame_observer=grid_capture;
  CHECK(show_guide_grid()==-1);frame_observer=NULL;
 }
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("EPG grid: %u checks; aligned vertical timeline, red current-time line, ten-minute automatic motion, manual scrolling, return to Now, missing-guide column and all eight themes IT/EN passed.\n",checks);
}
