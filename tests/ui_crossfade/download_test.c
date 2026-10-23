#define main iptv_ui_main
#define ui_message_show stub_message_show
#define ui_loading_run stub_loading_run
#include "../iptv/ui_test.c"
#undef main
#undef ui_message_show
#undef ui_loading_run
void ui_message_show(const char *,const char *,int);
int ui_loading_run(const char *,UiLoadingTaskFn,void *,volatile int *,const volatile long *,const volatile long *);
#include "../../src/ui/loading_screen.c"
#include "../../src/ui/format.c"
static int header_loading,hidden_download;
int ui_runtime_is_ready(void){return 1;}
void ui_brand_set_loading(int n){header_loading=n;}
void ui_brand_set_download_progress_hidden(int n){hidden_download=n;}
void vita2d_draw_fill_circle(float x,float y,float r,unsigned c){for(int dy=-(int)r;dy<=r;dy++)for(int dx=-(int)r;dx<=r;dx++)if(dx*dx+dy*dy<=r*r)pixel(x+dx,y+dy,c,255);}
int ui_font_draw_text_centered(vita2d_font *font,int cx,int y,int max,unsigned c,unsigned size,const char *text){
 char fitted[512];int w=ui_font_fit_text(font,size,text,fitted,sizeof(fitted),max);
 return ui_font_draw_text(font,cx-w/2,y,c,size,fitted);
}
static void progress_read(void *ctx,uint64_t *current,uint64_t *total){
 *current=3ULL*1024*1024*1024;*total=ctx?7ULL*1024*1024*1024:0;
}
static void frozen_canvas(void *ctx) {
 (void)ctx;
 vita2d_draw_rectangle(0,0,960,544,RGBA8(40,80,120,255));
 vita2d_draw_rectangle(0,360,960,184,RGBA8(34,100,63,255));
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 const char *title="A long resolved Content-Disposition filename — Podcast episode 127.m4a";
 for(language=0;language<2;language++)for(int paused=0;paused<2;paused++)for(int known=0;known<2;known++){
  frame=120;UiDownloadRateState rate={.sample_time_us=1,.sample_bytes=3ULL*1024*1024*1024-2*1024*1024,.bytes_per_second=1024*1024};
  draw_loading_frame(NULL,vt_i18n_str(VT_STR_NETWORK_DOWNLOADING),NULL,NULL,1,1,paused,&rate,progress_read,known?&rate:NULL,title,1,NULL);
  CHECK(!header_loading&&!hidden_download);
  char name[80];snprintf(name,sizeof(name),"download-%s-%s-%s",paused?"paused":"active",known?"total":"unknown",language?"it":"en");png(name);
 }
 frame=0;ui_message_show_success("Download complete",title,1);png("download-complete");
 frame=0;ui_message_show("Connection unavailable","Check the server address and your connection.",1);png("download-error");
 for(language=0;language<2;language++) {
  UiPlayerLoadingInfo loading={.title="Video",.status="Loading...",.cancellable=1,
      .draw_backdrop=frozen_canvas};
  vita2d_clear_screen();ui_player_loading_draw(&loading,NULL,NULL,1000000);
  CHECK(pixels[100][100][0]==40&&pixels[100][100][1]==80&&pixels[100][100][2]==120);
  CHECK(pixels[400][100][1]==100); /* The canvas survives outside the spinner. */
  png(language?"seek-preview-it":"seek-preview-en");
 }
 FT_Done_Face(face);FT_Done_FreeType(ft);printf("Crossfade native download states: %u checks passed\n",checks);
}
