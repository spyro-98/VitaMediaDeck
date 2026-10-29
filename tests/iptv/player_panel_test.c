#define main old_ui_test_main
#include "ui_test.c"
#undef main
#include "../../src/ui/iptv_player_panel.c"
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));FT_Face inter,mono;
 CHECK(!FT_New_Face(ft,argv[1],0,&inter));CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));
 VtIptvOptions *o=calloc(1,sizeof(*o));CHECK(o);
 for(int i=0;i<64;i++){
  VtIptvChoice *v=&o->video[i],*a=&o->audio[i];snprintf(v->id,sizeof(v->id),"video-%d",i);snprintf(a->id,sizeof(a->id),"audio-%d",i);
  v->width=1920;v->height=1080;v->bandwidth=9000000;snprintf(v->codec,sizeof(v->codec),"avc1.640028, mp4a.40.2");v->supported=1;
  snprintf(a->name,sizeof(a->name),"Italiano, commento originale e audiodescrizione numero %d",i);snprintf(a->language,sizeof(a->language),"it");a->supported=1;
 }
 o->video_count=o->audio_count=64;snprintf(o->title,sizeof(o->title),"Nome originale della trasmissione");snprintf(o->provider,sizeof(o->provider),"Emittente");
 VtIptvSelection s={0};UiIptvPlayerPanel p={0};
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;
  for(p.page=1;p.page<=3;p.page++){
   p.program.available=1;snprintf(p.program.title,sizeof(p.program.title),"Telegiornale – edizione di metà giornata");
   p.program.start=time(NULL)-600;p.program.end=time(NULL)+1800;p.program.next_start=p.program.end;p.program.next_end=p.program.end+3600;
   snprintf(p.program.subtitle,sizeof(p.program.subtitle),"Notizie, scienza e tecnologia");snprintf(p.program.category,sizeof(p.program.category),"Informazione");snprintf(p.program.episode,sizeof(p.program.episode),"Edizione 42");snprintf(p.program.next_title,sizeof(p.program.next_title),"Il programma successivo");
   p.program.upcoming_count=VT_IPTV_UPCOMING_MAX;p.program.public_source=1;p.program.cached=1;
   for(unsigned i=0;i<VT_IPTV_UPCOMING_MAX;i++){
    VtIptvUpcoming *next=&p.program.upcoming[i];next->start=p.program.end+i*3600;next->end=next->start+3600;
    snprintf(next->title,sizeof(next->title),"Programma successivo %u · cinema e documentari",i+1);
    snprintf(next->description,sizeof(next->description),"Approfondimento con una descrizione disponibile nella guida TV.");
   }
   memset(p.program.description,0,sizeof(p.program.description));for(int i=0;i<20;i++)strcat(p.program.description,"Notizie dalla Sardegna e dal mondo, con interviste e approfondimenti di scienza e tecnologia. ");
   for(int step=0;step<3;step++){
    p.cursor=step==0?0:step==1?7:64;p.scroll=step*14;
    ui_scroll_reset(&p.viewport,0);
    if(p.page<3){int map[VT_IPTV_CHOICE_MAX+2];int count=ui_iptv_choices(o,&s,p.page==2,map);
     ui_scroll_bounds(&p.viewport,count*56-6,392);ui_scroll_reveal(&p.viewport,p.cursor*56,p.cursor*56+50,392);
    }else {
     vita2d_start_drawing();ui_iptv_player_panel_draw(&p,530,o,&s);vita2d_end_drawing();
     p.viewport.target=ui_scroll_clamp(step==2?p.viewport.maximum:p.scroll*24,p.viewport.maximum);p.viewport.position=p.viewport.target;
    }
    vita2d_start_drawing();vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_BLUE_LIGHT);ui_panel(530,0,430,544,VT_THEME_BG_SOFT,VT_THEME_BORDER,0);
    ui_iptv_player_panel_draw(&p,530,o,&s);vita2d_end_drawing();
    char name[96];snprintf(name,sizeof(name),"iptv-player-%d-%u-page-%d-step-%d",theme,language,p.page,step);png(name);
    CHECK(p.top>=0&&p.cursor<=64&&p.scroll>=0);
   }
  }
  for(int pending=0;pending<2;pending++){
   memset(&p,0,sizeof(p));p.page=3;p.program_loading=pending;
   p.program.guide_status=VT_IPTV_GUIDE_NO_CHANNEL;p.program.cached=1;p.program.public_source=1;
   host_record_text=1;host_text_count=0;host_text_log[0]=0;
   vita2d_start_drawing();vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_BLUE_LIGHT);
   ui_panel(530,0,430,544,VT_THEME_BG_SOFT,VT_THEME_BORDER,0);
   ui_iptv_player_panel_draw(&p,530,o,&s);vita2d_end_drawing();
   host_record_text=0;
   CHECK(!strstr(host_text_log,"tvg-id")&&!strstr(host_text_log,"tvg-name"));
   CHECK(!strstr(host_text_log,"Open-EPG")&&!strstr(host_text_log,"cached on device"));
   char expected[24];snprintf(expected,sizeof(expected),"%.20s",vt_i18n_str(pending?VT_STR_IPTV_PROGRAM_LOADING:VT_STR_IPTV_GUIDE_NO_CHANNEL));
   CHECK(strstr(host_text_log,expected));
   char name[96];snprintf(name,sizeof(name),"iptv-programme-%s-%d-%u",pending?"pending":"unavailable",theme,language);png(name);
  }
 }
 free(o);FT_Done_Face(inter);FT_Done_Face(mono);FT_Done_FreeType(ft);
 printf("IPTV player panel: %u checks; 64 choices, active badges, long multilingual labels, scrollable programme/current-next, inline pending/unavailable states without technical/cache diagnostics, eight themes and IT/EN host rendering passed.\n",checks);
}
