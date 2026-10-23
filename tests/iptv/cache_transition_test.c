/* Delayed-worker scheduler: navigation must not wait for unfinished I/O. */
#define VMD_CACHE_TRANSITION_TEST
#include "catalog_ui_test.c"
static struct {int(*worker)(SceSize,void*);int started,done;} workers[128];
static int next_worker=1, waits, saved_reads, cache_hit, fail_fetch;
SceUID sceKernelCreateThread(const char*n,int(*f)(SceSize,void*),int p,size_t s,int a,int b,void*c){
 (void)n;(void)p;(void)s;(void)a;(void)b;(void)c;CHECK(next_worker<128);workers[next_worker].worker=f;return next_worker++;
}
int sceKernelStartThread(SceUID id,SceSize n,void *ctx){(void)n;(void)ctx;workers[id].started=1;return 0;}
int sceKernelWaitThreadEnd(SceUID id,void*a,void*b){(void)a;(void)b;CHECK(workers[id].done);waits++;return 0;}
int sceKernelDeleteThread(SceUID id){CHECK(workers[id].done||!workers[id].started);return 0;}
static void finish(int id){CHECK(id>0&&workers[id].started&&!workers[id].done);workers[id].worker(0,NULL);workers[id].done=1;}
int vt_online_load_saved(VtOnlineItem *items,int capacity){
 CHECK(capacity>=2);saved_reads++;memset(items,0,2*sizeof(*items));
 items[0].kind=VT_ONLINE_RADIO;strcpy(items[0].url,"https://example.org/radio");strcpy(items[0].title,"Radio refreshed");
 items[1].kind=VT_ONLINE_FEED;strcpy(items[1].url,"https://example.org/feed");strcpy(items[1].title,"Podcast refreshed");return 2;
}
int vt_online_fetch_query(void *ctx){VtOnlineQuery*q=ctx;if(q->cancel||fail_fetch)return -1;q->count=1;strcpy(q->items[0].title,"Fresh");return 0;}
int vt_online_query_cache_read(VtOnlineQuery*q){if(!cache_hit)return -1;q->count=1;q->items[0].kind=q->kind;strcpy(q->items[0].title,"Cached");strcpy(q->items[0].url,"https://example.org/cached");return 0;}
int vt_online_query_cache_write(const VtOnlineQuery*q){(void)q;return 0;}
int main(int argc,char**argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 ui_theme_apply(VT_UI_THEME_GHOST_MESH);g_home=1;g_tab=VT_ONLINE_RADIO;g_query.kind=VT_ONLINE_RADIO;
 load_saved();CHECK(saved_reads==1&&g_query.count==1);strcpy(g_query.items[0].artwork_url,"https://example.org/cover.png");
 cover_tick(6);int cover=g_cover_thread;CHECK(cover>0);
 lazy_tick(6);int query=g_lazy_thread;CHECK(query>0);
 VtOnlineQuery *held=malloc(sizeof(*held));CHECK(held);*held=g_lazy;
 g_home_cache[1].valid=1;g_home_cache[1].count=1;g_home_cache[1].items[0].kind=VT_ONLINE_FEED;
 strcpy(g_home_cache[1].items[0].title,"Podcast cached");strcpy(g_home_cache[1].items[0].url,"https://example.org/feed");
 CHECK(online_leave(UI_SECTION_LOCAL_MEDIA)==UI_SECTION_LOCAL_MEDIA);CHECK(!waits&&g_lazy.cancel&&g_cover_cancel);
 g_tab=VT_ONLINE_FEED;load_saved();CHECK(saved_reads==1&&!strcmp(g_query.items[0].title,"Podcast cached"));
 lazy_tick(6);CHECK(g_lazy_thread==query&&g_lazy.kind==held->kind);CHECK(!waits);
 finish(query);lazy_tick(6);CHECK(waits==1&&g_lazy_thread!=query);
 CHECK(!strcmp(g_query.items[0].title,"Podcast cached"));int refresh=g_lazy_thread;
 finish(cover);cover_tick(6);CHECK(g_query.kind==VT_ONLINE_FEED&&!strcmp(g_query.items[0].title,"Podcast cached"));
 finish(refresh);lazy_tick(6);CHECK(!strcmp(g_query.items[0].title,"Podcast refreshed"));
 CHECK(g_home_cache[1].valid&&!strcmp(g_home_cache[1].items[0].title,"Podcast refreshed"));
 cache_hit=1;int reads=saved_reads;
 CHECK(fetch_page(VT_ONLINE_EPISODE,"https://example.org/feed",0,1)==0);
 CHECK(!strcmp(g_query.items[0].title,"Cached")&&g_refresh_pending==1&&saved_reads==reads);
 lazy_tick(6);query=g_lazy_thread;fail_fetch=1;finish(query);lazy_tick(6);
 CHECK(!strcmp(g_query.items[0].title,"Cached")&&g_episodes);fail_fetch=0;
 /* Every media row asks for its thumbnail in each compact native theme. */
 for(int theme=1;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(int mini=0;mini<2;mini++)for(int kind=VT_ONLINE_RADIO;kind<=VT_ONLINE_EPISODE;kind++){
  ui_theme_apply(theme);ui_home_tab_set(kind==VT_ONLINE_RADIO?HOME_RADIO:HOME_PODCAST);mini_top=mini?472:544;memset(&g_query,0,sizeof(g_query));g_query.kind=kind;g_query.count=8;
  g_home=1;g_catalog=g_menu=0;g_episodes=kind==VT_ONLINE_EPISODE;g_tab=kind==VT_ONLINE_RADIO?kind:VT_ONLINE_FEED;g_top=g_selected=0;
  for(int i=0;i<8;i++){g_query.items[i].kind=kind;strcpy(g_query.items[i].title,"Long podcast or radio title for layout");strcpy(g_query.items[i].detail,"Publisher and programme");strcpy(g_query.items[i].artwork_path,"fixture.png");}
  UiSectionsSidebar side;ui_sections_sidebar_init(&side,UI_SECTION_LOCAL_MEDIA);UiContextMenu menu={0};UiFocusMotion focus={0};
  artwork_requests=0;frame=0;draw_themed_online(1,&focus,&side,&menu);
  CHECK(artwork_requests>=(unsigned)HOME_LAYOUT.rows);
  if(!mini&&language){char name[80];snprintf(name,sizeof(name),"compact-covers-%d-kind%d",theme,kind);png(name);}
 }
 free(held);FT_Done_Face(face);FT_Done_FreeType(ft);printf("Cached navigation: %u checks, held-worker exits, stale replies, refresh failures, all compact covers passed.\n",checks);return 0;
}
