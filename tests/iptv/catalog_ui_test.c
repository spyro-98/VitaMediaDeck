/* Use the existing native host renderer with the production online screen. */
#define VMD_ONLINE_UI
#define main iptv_ui_main
#include "ui_test.c"
#undef main
#include "../../src/ui/home_tabs.c"
#include "../../src/ui/online_audio_screen.c"
void ui_brand_draw_header_placeholder(const char *path,const char *title){(void)path;ui_brand_draw_header(title);}
void ui_brand_set_loading(int loading){(void)loading;}
#ifndef VMD_CACHE_TRANSITION_TEST
int vt_online_fetch_query(void *ctx){(void)ctx;return -1;}
int vt_online_load_saved(VtOnlineItem *out,int cap){(void)out;(void)cap;return 0;}
#endif
int vt_online_save(const VtOnlineItem *item){(void)item;return 0;}
int vt_online_remove(int kind,const char *url){(void)kind;(void)url;return 0;}
int vt_online_url_valid(const char *url){(void)url;return 1;}
int vt_online_resolve_show(VtOnlineItem *item,volatile int *cancel){(void)item;(void)cancel;return 0;}
int vt_background_playback_snapshot(VtBackgroundPlaybackSnapshot *out){memset(out,0,sizeof(*out));return 0;}
int vt_background_playback_prepare_radio(const char*a,const char*b){(void)a;(void)b;return 0;}
int vt_background_playback_prepare_podcast(const char*a,const char*b){(void)a;(void)b;return 0;}
void vt_background_playback_set_audio_details(const char*a,const char*b){(void)a;(void)b;}
uint64_t vt_background_playback_set_podcast_history(const char*a,uint64_t duration){(void)a;(void)duration;return 0;}
int vt_background_playback_activate(uint64_t n){(void)n;return 0;}
int ui_music_player_run(const char*a,const char*b,const char*c,uint32_t d){(void)a;(void)b;(void)c;(void)d;return 0;}
int ui_text_reader_run(const char*a,const char*b){(void)a;(void)b;return UI_SECTION_NONE;}
void ui_message_show_success(const char*a,const char*b,int ms){(void)a;(void)b;(void)ms;}
int ui_folder_picker(const char*a,char*b,size_t c){(void)a;(void)b;(void)c;return 0;}
void vt_download_job_init_url(VtDownloadJob*a,const char*b){(void)a;(void)b;}
void vt_download_job_set_destination(VtDownloadJob*a,const char*b){(void)a;(void)b;}
int vt_download_background_start(const VtDownloadJob*a){(void)a;return 0;}
#ifndef VMD_CACHE_TRANSITION_TEST
int main(int argc,char **argv) {
    CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
    online_case=1;loaded=1;g_initialized=1;
    const char *countries[]={"Italy","Japan","United Kingdom","United States","France","Germany","Portugal","Spain","New Zealand"};
    const char *codes[]={"IT","JP","GB","US","FR","DE","PT","ES","NZ"};
    for(int mini=0;mini<2;mini++)for(language=0;language<2;language++)for(int kind=0;kind<6;kind++) {
        mini_top=mini?544-UI_MINI_PLAYER_HEIGHT:544;
        frame=0;g_home=kind==3;g_tab=kind==3?VT_ONLINE_FEED:VT_ONLINE_RADIO;
        ui_home_tab_set(kind==3?HOME_PODCAST:HOME_RADIO);
        g_mode_changed=g_menu=g_episodes=0;g_catalog=kind<3;
        if(kind==4)g_menu=1;
        if(kind==5){g_episodes=1;g_tab=VT_ONLINE_FEED;}
        memset(&g_query,0,sizeof(g_query));g_query.kind=kind==0?VT_ONLINE_COUNTRY:kind==1?VT_ONLINE_TAG:kind==2?VT_ONLINE_RADIO:VT_ONLINE_FEED;
        if(kind==4)g_query.kind=VT_ONLINE_MENU;
        if(kind==5)g_query.kind=VT_ONLINE_EPISODE;
        g_query.count=kind==0?9:kind==4?4:6;g_top=g_selected=0;g_pages=1;g_page_sizes[0]=g_query.count;
        for(int i=0;i<g_query.count;i++) {
            VtOnlineItem *item=&g_query.items[i];item->kind=g_query.kind;
            snprintf(item->title,sizeof(item->title),"%s",kind==0?countries[i]:kind==1?"Electronic / Alternative":kind==2?"Radio station - evening programme":"Science and history podcast");
            snprintf(item->id,sizeof(item->id),"%s",kind==0?codes[i]:"");
            snprintf(item->detail,sizeof(item->detail),"%s",kind==0?codes[i]:kind==1?"":"Publisher / Description");
            if(kind>=2)strcpy(item->artwork_path,"fixture-square.png");
            if(kind==5) {
                snprintf(item->title,sizeof(item->title),"Episode title with enough text to exercise clipping before the date and duration column on the right");
                snprintf(item->detail,sizeof(item->detail),"Author with a long name / Additional episode details that must not overlap publication information");
                if(i!=2)strcpy(item->published_date,"2026-09-07");
                item->duration_ms=i==1?185000:i==2?0:3723000;
            }
        }
        if(kind==4)browse_menu();
        CHECK(ui_online_audio_screen()==(kind==3?UI_SECTION_NETWORK:UI_SECTION_IPTV));
        memcpy(pixels,first_pixels,sizeof(pixels));char name[64];snprintf(name,sizeof(name),"catalog-%d-%s%s",kind,language?"it":"en",mini?"-mini":"");png(name);
        int rows=CATALOG_ROWS;
        CHECK(LIST_Y+rows*CATALOG_STEP-UI_CONTENT_GAP<=CATALOG_BOTTOM-28);
        UiContextAction actions[8];int count=online_options(actions,0);CHECK(count<=8);
        int download=0,search=0;
        for(int i=0;i<count;i++){download+=actions[i].action==SCE_CTRL_START;search+=actions[i].action==SCE_CTRL_TRIANGLE;}
        if(kind==3)CHECK(!download&&!search);
        if(kind==5)CHECK(download==1&&!search);
        UiContextMenu options={.open=1,.animation=1};
        ui_context_menu_draw(&options,TR(UI_OPTIONS),actions,count);
        snprintf(name,sizeof(name),"catalog-options-%d-%s%s",kind,language?"it":"en",mini?"-mini":"");png(name);
    }
    /* The left half of the Podcast tab used to fall in Radio's old hit area.
     * Search/Back also occupied the old tab rectangle. */
    char date[16],duration[24];VtOnlineItem episode={0};
    episode_labels(&episode,date,duration);CHECK(!strcmp(date,"—")&&!strcmp(duration,"—"));
    strcpy(episode.published_date,"2026-09-07");episode.duration_ms=3723000;
    episode_labels(&episode,date,duration);CHECK(!strcmp(date,"2026-09-07")&&!strcmp(duration,"1:02:03"));
    episode.duration_ms=185000;episode_labels(&episode,date,duration);CHECK(!strcmp(duration,"3:05"));
    mini_top=544;g_home=0;g_mode_changed=0;g_tab=VT_ONLINE_RADIO;browse_menu();
    frame=0;tap_frame=0;tap_x=240;tap_y=65;
    CHECK(ui_online_audio_screen()==UI_SECTION_IPTV && g_tab==VT_ONLINE_FEED);
    frame=0;tap_frame=0;tap_x=450;tap_y=65;
    CHECK(ui_online_audio_screen()==UI_SECTION_LOCAL_MEDIA && g_home && g_tab==VT_ONLINE_FEED);
    FT_Done_Face(face);FT_Done_FreeType(ft);printf("Catalogue host UI: %u checks passed\n",checks);
}
#endif

#ifndef VMD_CACHE_TRANSITION_TEST
int vt_online_query_cache_read(VtOnlineQuery *q){(void)q;return -1;}
int vt_online_query_cache_write(const VtOnlineQuery *q){(void)q;return 0;}
#endif
