/* Use the existing native host renderer with the production online screen. */
#define VMD_ONLINE_UI
#define main iptv_ui_main
#include "ui_test.c"
#undef main
#include "../../src/ui/home_tabs.c"
#include "../../src/ui/online_audio_screen.c"
void ui_brand_draw_header_placeholder(const char *path,const char *title){(void)path;ui_brand_draw_header(title);}
void ui_brand_set_loading(int loading){(void)loading;}
int vt_online_fetch_query(void *ctx){(void)ctx;return -1;}
static VtOnlineItem favorites[VT_ONLINE_MAX];static int favorite_count,favorite_write_error;
int vt_online_load_saved(VtOnlineItem *out,int cap){int n=favorite_count<cap?favorite_count:cap;memcpy(out,favorites,n*sizeof(*out));return n;}
int vt_online_save(const VtOnlineItem *item){if(favorite_write_error)return -1;CHECK(favorite_count<VT_ONLINE_MAX);favorites[favorite_count++]=*item;return 0;}
int vt_online_remove(int kind,const char *url){if(favorite_write_error)return -1;int n=0;for(int i=0;i<favorite_count;i++)if(favorites[i].kind!=kind||strcmp(favorites[i].url,url))favorites[n++]=favorites[i];favorite_count=n;return 0;}
int vt_online_url_valid(const char *url){(void)url;return 1;}
int vt_online_resolve_show(VtOnlineItem *item,volatile int *cancel){(void)item;(void)cancel;return 0;}
int vt_background_playback_snapshot(VtBackgroundPlaybackSnapshot *out){memset(out,0,sizeof(*out));return 0;}
int vt_background_playback_prepare_radio(const char*a,const char*b){(void)a;(void)b;return 0;}
int vt_background_playback_prepare_podcast(const char*a,const char*b){(void)a;(void)b;return 0;}
void vt_background_playback_set_audio_details(const char*a,const char*b){(void)a;(void)b;}
uint64_t vt_background_playback_set_podcast_history(const char*a){(void)a;return 0;}
int vt_background_playback_activate(uint64_t n){(void)n;return 0;}
static unsigned audio_returns;
int ui_music_player_run(const char*a,const char*b,const char*c,uint32_t d){(void)a;(void)b;(void)c;(void)d;audio_returns++;return 0;}
int ui_text_reader_run(const char*a,const char*b){(void)a;(void)b;return UI_SECTION_NONE;}
void ui_message_show_success(const char*a,const char*b,int ms){(void)a;(void)b;(void)ms;}
int ui_folder_picker(const char*a,char*b,size_t c){(void)a;(void)b;(void)c;return 0;}
void vt_download_job_init_url(VtDownloadJob*a,const char*b){(void)a;(void)b;}
void vt_download_job_set_destination(VtDownloadJob*a,const char*b){(void)a;(void)b;}
int vt_download_background_start(const VtDownloadJob*a){(void)a;return 0;}
static void fixture(int kind){
 clear_history();g_saved_changed=g_saved_valid=0;favorite_count=favorite_write_error=0;
 memset(&g_query,0,sizeof(g_query));g_home=kind==0||kind==1||kind==5;
 g_tab=kind==1||kind==5?VT_ONLINE_FEED:VT_ONLINE_RADIO;
 g_episodes=kind==5;g_menu=kind==2;g_catalog=kind==3||kind==4;
 g_return_valid=g_mode_changed=0;g_initialized=1;g_top=g_selected=0;
 g_query.kind=kind==2?VT_ONLINE_MENU:kind==3?VT_ONLINE_COUNTRY:kind==4?VT_ONLINE_TAG:
    kind==5?VT_ONLINE_EPISODE:g_tab;
 g_query.count=6;g_page_sizes[0]=6;g_pages=1;g_home_list_loaded=1;
 for(int i=0;i<6;i++){
  VtOnlineItem *item=&g_query.items[i];item->kind=g_query.kind;
  snprintf(item->title,sizeof(item->title),"%s %d",kind==0?"Radio station":kind==5?"Podcast episode":"Science and history",i+1);
  snprintf(item->url,sizeof(item->url),"https://example.org/item/%d",i);
  strcpy(item->detail,"Publisher / description");strcpy(item->artwork_path,"fixture-square.png");
  if(kind==5){strcpy(item->published_date,"2026-10-23");item->duration_ms=185000;}
 }
 strcpy(g_feed_title,"Science and history");
 ui_home_tab_set(g_tab==VT_ONLINE_RADIO?HOME_RADIO:HOME_PODCAST);
 ui_home_tabs_set_expanded(0);ui_home_controls_reset();
 frame=0;axis_count=0;tap_frame=-1;compact_home_labels=0;online_case=0;
 memset(input_script,0,sizeof(input_script));
}
static void exit_via_sidebar(unsigned after){
 CHECK(after+5<16);script_count=after+6;
 input_script[after+1]=SCE_CTRL_LTRIGGER;
 input_script[after+3]=SCE_CTRL_DOWN;
 input_script[after+5]=SCE_CTRL_CROSS;
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));loaded=1;
 FT_Face inter=face,mono;CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));
 for(int theme=1;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(int mini=0;mini<2;mini++)for(int kind=0;kind<6;kind++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;mini_top=mini?472:544;
  fixture(kind);exit_via_sidebar(0);capture_frame=0;
  int result=ui_online_audio_screen();CHECK(result==UI_SECTION_NETWORK||result==UI_SECTION_IPTV);
  CHECK(!compact_home_labels);CHECK(ui_theme_id()==theme);
  memcpy(pixels,first_pixels,sizeof(pixels));char name[100];
  snprintf(name,sizeof(name),"theme-online-%d-%d-%s%s",theme,kind,language?"it":"en",mini?"-mini":"");png(name);
  /* A return from discovery must go to the same themed Home. */
  if(kind==2){fixture(kind);input_script[1]=SCE_CTRL_CIRCLE;script_count=2;
   CHECK(ui_online_audio_screen()==UI_SECTION_LOCAL_MEDIA&&g_home);
   CHECK(!compact_home_labels);UiThemeRect tab=ui_theme_home_tab_rect(theme,3,0);CHECK(tab.y>=62);
  }
  /* An episode's parent is restored without exposing the legacy toolbar. */
  if(kind==5){fixture(kind);g_return_valid=1;g_return_query=g_query;g_return_query.kind=VT_ONLINE_FEED;
   g_return_query.count=2;g_return_selected=g_return_top=g_return_catalog=0;g_return_pages=1;g_return_sizes[0]=2;
   input_script[1]=SCE_CTRL_CIRCLE;exit_via_sidebar(2);capture_frame=2;
   result=ui_online_audio_screen();CHECK(result==UI_SECTION_NETWORK);
   CHECK(!g_episodes&&g_query.kind==VT_ONLINE_FEED&&!g_return_valid&&!compact_home_labels);
  }
 }
 for(int theme=1;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);fixture(1);mini_top=544;
  g_selected=g_query.count;g_top=vt_catalog_top(g_selected,0,CATALOG_COLUMNS,CATALOG_ROWS);
  UiThemeRect hit=online_item_rect(g_selected-g_top);
  tap_frame=0;tap_x=hit.x+20;tap_y=hit.y+20;script_count=2;
  CHECK(ui_online_audio_screen()==UI_SECTION_LOCAL_MEDIA);
  CHECK(!ui_home_tab_online()&&ui_home_tab()==HOME_PODCAST&&!compact_home_labels);
 }
 /* Returning from the real playback branch keeps the same themed section. */
 allow_audio_fullscreen=1;
 for(int theme=1;theme<VT_UI_THEME_COUNT;theme++)for(int kind=0;kind<2;kind++){
  ui_theme_apply(theme);fixture(kind?5:0);mini_top=544;capture_frame=2;
  UiThemeRect hit=online_item_rect(0);tap_frame=1;tap_x=hit.x+20;tap_y=hit.y+20;
  exit_via_sidebar(2);unsigned before=audio_returns;
  CHECK(ui_online_audio_screen()==UI_SECTION_NETWORK);
  CHECK(audio_returns==before+1&&ui_theme_id()==theme&&g_query.count==6&&!compact_home_labels);
 }
 allow_audio_fullscreen=0;
 /* Held right stick: Radio -> Podcast -> Images in the actual online loop. */
 ui_theme_apply(VT_UI_THEME_GHOST_MESH);fixture(0);mini_top=544;
 axis_count=6;script_count=6;
 for(unsigned i=0;i<axis_count;i++)input_axes[i]=(SceCtrlData){.lx=128,.ly=128,.rx=128,.ry=128};
 input_axes[1].rx=input_axes[2].rx=18;input_axes[1].ry=input_axes[2].ry=192;
 input_axes[4].rx=18;input_axes[4].ry=64;
 CHECK(ui_online_audio_screen()==UI_SECTION_LOCAL_MEDIA);
 CHECK(ui_home_tab()==HOME_IMAGES&&g_tab==VT_ONLINE_FEED&&!compact_home_labels);
 axis_count=0;
 /* The same R1 Remove action works in Home for every theme, language and provider. */
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(int provider=0;provider<2;provider++) {
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;fixture(provider);mini_top=544;
  favorite_count=g_query.count;memcpy(favorites,g_query.items,favorite_count*sizeof(*favorites));
  unsigned sequence[]={0,SCE_CTRL_RTRIGGER,0,SCE_CTRL_DOWN,0,SCE_CTRL_CROSS};
  memcpy(input_script,sequence,sizeof(sequence));exit_via_sidebar(6);capture_frame=6;
  CHECK(ui_online_audio_screen()==UI_SECTION_NETWORK);
  CHECK(g_query.count==5&&favorite_count==5&&g_selected==0);
  CHECK(!strcmp(g_query.items[0].url,"https://example.org/item/1"));
  CHECK(!g_lazy.cancel || g_lazy_thread<0 || g_lazy_generation!=g_context_generation);
  /* Discovery toggles the saved state without removing the catalogue result. */
  fixture(provider);g_home=0;g_catalog=1;g_menu=0;g_query.count=1;
  CHECK(toggle_favorite()==1&&favorite_count==1&&g_query.count==1);
  CHECK(favorite_index(&g_query.items[0])>=0);
  UiContextAction actions[8];int action_count=online_options(actions,0),remove_actions=0;
  for(int i=0;i<action_count;i++)remove_actions+=actions[i].action==SCE_CTRL_SQUARE;
  CHECK(action_count<=8&&remove_actions==1);
  CHECK(toggle_favorite()==1&&!favorite_count&&g_query.count==1);
  CHECK(favorite_index(&g_query.items[0])<0);
  /* Last-item removal stays empty and cannot resurrect a cached favourite. */
  fixture(provider);g_home_list_loaded=1;g_query.count=1;favorite_count=1;favorites[0]=g_query.items[0];
  CHECK(toggle_favorite()==1&&!favorite_count&&!g_query.count);
  load_saved();CHECK(!g_query.count&&g_home_cache[provider].valid);
  /* A failed storage update leaves the selected favourite and cache intact. */
  fixture(provider);favorite_count=1;favorites[0]=g_query.items[0];g_query.count=1;favorite_write_error=1;
  CHECK(toggle_favorite()<0&&g_query.count==1&&favorite_count==1);
 }
 /* Discovery keeps provider tabs: the Ghost wheel cannot route into another Home section. */
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++) {
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;fixture(2);mini_top=544;
  axis_count=6;for(unsigned i=0;i<axis_count;i++)input_axes[i]=(SceCtrlData){.lx=128,.ly=128,.rx=18,.ry=192};
  exit_via_sidebar(0);int section=ui_online_audio_screen();CHECK(section==UI_SECTION_NETWORK||section==UI_SECTION_IPTV);
  CHECK(!g_home&&g_tab==VT_ONLINE_RADIO&&ui_home_tab()==HOME_RADIO);axis_count=0;
 }
 clear_history();face=inter;FT_Done_Face(mono);ui_theme_apply(VT_UI_THEME_LEGACY);
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Themed online flows: %u checks, seven themes, favourite removal/failure/empty state, isolated provider inputs and nested flows passed\n",checks);
}

int vt_online_query_cache_read(VtOnlineQuery *q){(void)q;return -1;}
int vt_online_query_cache_write(const VtOnlineQuery *q){(void)q;return 0;}
