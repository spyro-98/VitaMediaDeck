#include "library/catalog.h"
#include "ui/theme_scene.h"
#include "ui/online_audio_screen.h"
#include "ui/online_catalog_layout.h"
#include "ui/frame_pacing.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include "app_paths.h"
#include "common/text_log.h"
#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "media/video_thumbnail.h"
#include "network/online_audio.h"
#include "network/download_manager.h"
#include "ui/brand.h"
#include "ui/home_tabs.h"
#include "ui/components.h"
#include "ui/folder_picker.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/music_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/text_reader.h"
#include "ui/theme.h"
#include "ui/theme_home.h"
#include "ui/touch.h"

static VtOnlineQuery g_query;
static int g_tab = VT_ONLINE_RADIO;
static int g_episodes, g_selected, g_top, g_initialized;
static char g_feed_title[160];
static int g_catalog, g_return_valid, g_return_selected, g_return_top, g_return_catalog;
static VtOnlineQuery g_return_query;
static char g_country_name[160] = "";
static int g_home, g_mode_changed=1, g_menu;
void ui_online_audio_set_home(int favorites_only) {g_home=!!favorites_only;g_mode_changed=1;}
#define THEMED (ui_theme_id()!=VT_UI_THEME_LEGACY)
#define HOME_LAYOUT (ui_theme_home_layout(ui_theme_id(),ui_mini_player_top()<UI_CONTENT_BOTTOM?ui_mini_player_top():UI_CONTENT_BOTTOM))
#define PODCAST_HOME (!THEMED && g_home && g_tab==VT_ONLINE_FEED && !g_episodes)
#define THEMED_OFFLINE (THEMED && g_home && g_tab==VT_ONLINE_FEED && !g_episodes && !g_menu && !g_catalog)
#define LIST_Y (THEMED ? HOME_LAYOUT.y : g_home ? (PODCAST_HOME ? 88 : UI_HOME_CONTENT_Y) : 110)
#define CATALOG_BOTTOM (ui_mini_player_top() - (g_home ? 24 : 0))
#define CATALOG_COVERS (!THEMED && !g_menu && !PODCAST_HOME && (g_query.kind==VT_ONLINE_RADIO || g_query.kind==VT_ONLINE_FEED || g_query.kind==VT_ONLINE_SHOW))
#define CATALOG_ROWS (THEMED ? HOME_LAYOUT.rows/HOME_LAYOUT.columns : CATALOG_COVERS ? 1 : vt_catalog_rows(LIST_Y,CATALOG_BOTTOM))
#define CATALOG_COLUMNS (THEMED ? HOME_LAYOUT.columns : PODCAST_HOME ? 1 : CATALOG_COVERS ? 3 : vt_catalog_columns(g_query.kind))
#define CATALOG_STEP (THEMED ? HOME_LAYOUT.step : (CATALOG_BOTTOM-28-LIST_Y+UI_CONTENT_GAP)/CATALOG_ROWS)
#define CATALOG_HEIGHT (THEMED ? HOME_LAYOUT.step-6 : CATALOG_STEP - UI_CONTENT_GAP)
#define OPTIONS_Y (g_home ? ui_mini_player_top() - 48 : UI_TABS_Y)
#define PODCAST_X (UI_CONTENT_X + vt_catalog_width(2) + UI_CONTENT_GAP)
#define ONLINE_ACTION_BROWSE (1<<24)
#define ONLINE_ACTION_REFRESH (1<<25)
#define ONLINE_ACTION_SAVED (1<<26)
#define ONLINE_ACTION_OFFLINE (1<<27)
static int favorite_index(const VtOnlineItem *item);
static int saved_list(void) { return !g_menu && !g_catalog && !g_episodes; }
static int can_favorite(const VtOnlineItem *item) {
    return item && (item->kind==VT_ONLINE_RADIO || item->kind==VT_ONLINE_FEED || item->kind==VT_ONLINE_SHOW);
}
#define TR(id) vt_i18n_str(VT_STR_##id)

static int online_options(UiContextAction items[8], int offline_focus) {
    int n=0;
    VtOnlineItem *item=g_selected>=0 && g_selected<g_query.count ? &g_query.items[g_selected] : NULL;
    int selected=item && !g_menu && g_query.kind!=VT_ONLINE_COUNTRY && g_query.kind!=VT_ONLINE_TAG && !(PODCAST_HOME&&offline_focus);
    if((PODCAST_HOME && offline_focus)||(THEMED_OFFLINE&&g_selected==g_query.count))
        items[n++]=(UiContextAction){TR(HOME_PODCAST_OPEN),NULL,ONLINE_ACTION_OFFLINE,1};
    if(selected) {
        items[n++]=(UiContextAction){g_tab==VT_ONLINE_RADIO||g_episodes?TR(IPTV_PLAY):TR(ONLINE_EPISODES),NULL,SCE_CTRL_CROSS,1};
        if(can_favorite(item))items[n++]=(UiContextAction){saved_list()||favorite_index(item)>=0?TR(UI_UNSTAR):TR(UI_STAR),NULL,SCE_CTRL_SQUARE,1};
        items[n++]=(UiContextAction){TR(UI_DETAILS),NULL,SCE_CTRL_SELECT,1};
        if(g_episodes)items[n++]=(UiContextAction){TR(UI_DOWNLOAD),NULL,SCE_CTRL_START,1};
    }
    if(!g_home && !g_episodes) {
        items[n++]=(UiContextAction){TR(ONLINE_SEARCH),NULL,SCE_CTRL_TRIANGLE,1};
        if(g_query.kind==VT_ONLINE_COUNTRY)items[n++]=(UiContextAction){g_tab==VT_ONLINE_RADIO?TR(ONLINE_WORLD):TR(ONLINE_US_STORE),NULL,SCE_CTRL_START,1};
        else if(g_query.kind!=VT_ONLINE_TAG)items[n++]=(UiContextAction){TR(ONLINE_ADD),NULL,SCE_CTRL_START,1};
    }
    if(g_menu || (!g_home && g_catalog))items[n++]=(UiContextAction){TR(ONLINE_FAVORITES),NULL,ONLINE_ACTION_SAVED,1};
    else if(!g_episodes)items[n++]=(UiContextAction){TR(ONLINE_BROWSE),NULL,ONLINE_ACTION_BROWSE,1};
    if(!g_menu)items[n++]=(UiContextAction){TR(UI_REFRESH),NULL,ONLINE_ACTION_REFRESH,1};
    if(!g_home || g_episodes)items[n++]=(UiContextAction){TR(UI_BACK),NULL,SCE_CTRL_CIRCLE,1};
    return n;
}
static VtOnlineQuery g_lazy;
static unsigned g_context_generation, g_lazy_generation, g_cover_generation;
static int g_refresh_pending,g_home_list_loaded;
static struct {
    VtOnlineItem items[VT_ONLINE_MAX];
    int count, valid;
} g_home_cache[2];
static struct { char url[512], id[192]; int kind; } g_saved_keys[VT_ONLINE_MAX];
static int g_saved_count, g_saved_valid, g_saved_changed;
static int refresh_favorites(void) {
    VtOnlineItem *items=calloc(VT_ONLINE_MAX,sizeof(*items));
    if(!items)return -1;
    int count=vt_online_load_saved(items,VT_ONLINE_MAX);
    if(count>=0) {
        g_saved_count=count;g_saved_valid=1;
        for(int i=0;i<count;i++) {
            snprintf(g_saved_keys[i].url,sizeof(g_saved_keys[i].url),"%s",items[i].url);
            snprintf(g_saved_keys[i].id,sizeof(g_saved_keys[i].id),"%s",items[i].id);
            g_saved_keys[i].kind=items[i].kind;
        }
    }
    free(items);return count<0?-1:0;
}
static int favorite_index(const VtOnlineItem *item) {
    if(!can_favorite(item))return -1;
    if(!g_saved_valid && refresh_favorites()<0)return -1;
    for(int i=0;i<g_saved_count;i++) {
        if(item->kind==VT_ONLINE_SHOW) {
            if(g_saved_keys[i].kind==VT_ONLINE_FEED && item->id[0] && !strcmp(item->id,g_saved_keys[i].id))return i;
        } else if(item->kind==g_saved_keys[i].kind && !strcmp(item->url,g_saved_keys[i].url))return i;
    }
    return -1;
}
/* Four extra parents plus the existing direct parent cover catalogue -> country
 * -> shows -> episodes. Allocate snapshots only while descending, never per frame. */
#define ONLINE_HISTORY_MAX 4
static struct OnlineParent {
    VtOnlineQuery query;
    int selected,top,catalog,pages,sizes[4];
    char country[160],feed[160];
} *g_history[ONLINE_HISTORY_MAX];
static int g_history_count;
static char g_return_country[160],g_return_feed[160];
static void clear_history(void) {
    while(g_history_count)free(g_history[--g_history_count]);
    g_return_valid=0;
}
static void cache_home(void) {
    if (!g_home_list_loaded || !g_home || g_menu || g_catalog || g_episodes ||
        (g_query.kind!=VT_ONLINE_RADIO && g_query.kind!=VT_ONLINE_FEED)) return;
    int slot=g_query.kind==VT_ONLINE_FEED;
    int count=g_query.count<VT_ONLINE_MAX?g_query.count:VT_ONLINE_MAX;
    memcpy(g_home_cache[slot].items,g_query.items,count*sizeof(VtOnlineItem));
    g_home_cache[slot].count=count;g_home_cache[slot].valid=1;
}
static void lazy_tick(int rows);

static SceUID g_lazy_thread = -1;
static volatile int g_lazy_done;
static int g_lazy_result, g_lazy_direction;
static int g_page_sizes[4], g_pages=1, g_return_sizes[4], g_return_pages;
static int g_lazy_failed_offset=-1;
static VtOnlineItem g_cover_item;
static SceUID g_cover_thread=-1;
static volatile int g_cover_cancel,g_cover_done;
static volatile int g_action_cancel;
static int cover_worker(SceSize args,void *ctx) {
	(void)args;(void)ctx;vt_online_fetch_cover(&g_cover_item,&g_cover_cancel);
	__sync_synchronize();g_cover_done=1;return sceKernelExitThread(0);
}
static void cover_stop(void) {
    /* The worker owns g_cover_item until done; navigation never joins I/O. */
    if(g_cover_thread>=0)g_cover_cancel=1;
}
static void cover_tick(int rows) {
	if(g_cover_thread>=0&&g_cover_done) {
		sceKernelWaitThreadEnd(g_cover_thread,NULL,NULL);sceKernelDeleteThread(g_cover_thread);g_cover_thread=-1;
		for(int i=0;i<g_query.count;i++)if(!g_cover_cancel && g_cover_generation==g_context_generation && !strcmp(g_query.items[i].url,g_cover_item.url)) {
			int recovered=!g_query.items[i].artwork_url[0]&&g_cover_item.artwork_url[0];
			if(!g_query.items[i].artwork_url[0])snprintf(g_query.items[i].artwork_url,sizeof(g_query.items[i].artwork_url),"%s",g_cover_item.artwork_url);
			snprintf(g_query.items[i].artwork_path,sizeof(g_query.items[i].artwork_path),"%s",g_cover_item.artwork_path[0]?g_cover_item.artwork_path:"-");
			VtLibraryItem library_item;
			vt_library_online(&g_query.items[i],
				              g_query.kind == VT_ONLINE_EPISODE ? g_query.query : NULL,
				              &library_item);
			vt_library_cache(&library_item);
			if (recovered && saved_list() && favorite_index(&g_query.items[i]) >= 0)
				vt_online_save(&g_query.items[i]);
			}
	}
	if (g_cover_thread >= 0)
		return;
	for (int r = -1; r < rows; r++) {
		int i = r < 0 ? g_selected : g_top + r;
		if (i < 0 || i >= g_query.count || g_query.items[i].artwork_path[0])
			continue;
		if (!g_query.items[i].artwork_url[0] &&
		    !(g_query.items[i].kind == VT_ONLINE_RADIO && g_query.items[i].id[0]) &&
		    g_query.items[i].kind != VT_ONLINE_FEED)
			continue;
		g_cover_item = g_query.items[i];
		g_cover_cancel = g_cover_done = 0;
		g_cover_generation = g_context_generation;
		g_cover_thread =
		    sceKernelCreateThread("VmdCover", cover_worker, 0x10000100, 0x40000, 0, 0, NULL);
		if (g_cover_thread >= 0 && sceKernelStartThread(g_cover_thread, 0, NULL) < 0) {
			sceKernelDeleteThread(g_cover_thread);
			g_cover_thread = -1;
		}
		if (g_cover_thread < 0)
			snprintf(g_query.items[i].artwork_path, sizeof(g_query.items[i].artwork_path), "-");
		break;
	}
}
static void episode_labels(const VtOnlineItem *item, char date[16], char duration[24]) {
	snprintf(date,16,"%s",item->published_date[0]?item->published_date:"—");
	uint64_t seconds=item->duration_ms/1000;
	if(!seconds)snprintf(duration,24,"—");
	else if(seconds>=3600)snprintf(duration,24,"%llu:%02u:%02u",
	    (unsigned long long)(seconds/3600),(unsigned)(seconds/60%60),(unsigned)(seconds%60));
	else snprintf(duration,24,"%u:%02u",(unsigned)(seconds/60),(unsigned)(seconds%60));
}
static int show_loading(void *ctx) {return vt_online_resolve_show(ctx,&g_action_cancel);}
static void library_query_cache(const VtOnlineQuery *q) {
	for (int i = 0; i < q->count; i++)
		if (q->items[i].kind == VT_ONLINE_RADIO || q->items[i].kind == VT_ONLINE_FEED ||
		    q->items[i].kind == VT_ONLINE_EPISODE) {
			VtLibraryItem v;
			vt_library_online(&q->items[i], q->kind == VT_ONLINE_EPISODE ? q->query : NULL, &v);
			vt_library_cache(&v);
		}
}
static int lazy_worker(SceSize args, void *ctx) {
	(void)args;(void)ctx;
    if(g_lazy_direction==2) {
        g_lazy.count=vt_online_load_saved(g_lazy.items,VT_ONLINE_MAX);
        g_lazy_result=g_lazy.count<0?-1:0;
        int count=0;
        for(int i=0;i<g_lazy.count;i++)if(g_lazy.items[i].kind==g_lazy.kind)g_lazy.items[count++]=g_lazy.items[i];
        g_lazy.count=count;
    } else {
        g_lazy_result=vt_online_fetch_query(&g_lazy);
        if(g_lazy.kind==VT_ONLINE_EPISODE&&!g_lazy.offset&&!g_lazy.count)g_lazy_result=-1;
		if (g_lazy_result >= 0 && !g_lazy.cancel) {
			vt_online_query_cache_write(&g_lazy);
			library_query_cache(&g_lazy);
		}
	}
	__sync_synchronize();g_lazy_done=1;return sceKernelExitThread(0);
}
static void lazy_stop(void) {
    g_context_generation++;g_refresh_pending=0;
    if(g_lazy_thread>=0)g_lazy.cancel=1;
}
static int online_leave(int section) {
    cache_home();lazy_stop();cover_stop();ui_brand_set_loading(0);return section;
}
static void merge_cover_paths(VtOnlineQuery *next) {
    for(int i=0;i<next->count;i++)for(int j=0;j<g_query.count;j++)
        if(!strcmp(next->items[i].url,g_query.items[j].url) &&
           !strcmp(next->items[i].artwork_url,g_query.items[j].artwork_url) &&
           g_query.items[j].artwork_path[0] && strcmp(g_query.items[j].artwork_path,"-")) {
            snprintf(next->items[i].artwork_path,sizeof(next->items[i].artwork_path),"%s",g_query.items[j].artwork_path);break;
        }
}
static void lazy_tick(int rows) {
	if(g_lazy_thread>=0 && g_lazy_done) {
		sceKernelWaitThreadEnd(g_lazy_thread,NULL,NULL);sceKernelDeleteThread(g_lazy_thread);g_lazy_thread=-1;
		if(g_lazy_result>=0 && !g_lazy.cancel && g_lazy_generation==g_context_generation) {
            if(g_lazy_direction==0 || g_lazy_direction==2) {
                char selected_url[512]="";
                if(g_selected<g_query.count)snprintf(selected_url,sizeof(selected_url),"%s",g_query.items[g_selected].url);
                merge_cover_paths(&g_lazy);g_query=g_lazy;g_query.cancel_parent=NULL;
                for(int i=0;i<g_query.count;i++)if(selected_url[0]&&!strcmp(selected_url,g_query.items[i].url)){g_selected=i;break;}
                if(g_selected>=g_query.count)g_selected=g_query.count?g_query.count-1:0;
                g_pages=1;g_page_sizes[0]=g_query.count;cache_home();
            } else if(g_lazy_direction>0) {
				if(g_pages==4) {int drop=g_page_sizes[0];memmove(g_query.items,g_query.items+drop,(g_query.count-drop)*sizeof(g_query.items[0]));g_query.count-=drop;g_selected-=drop;g_top-=drop;ui_theme_page_rebase(UI_SCENE_HOME,g_top+drop,g_top);g_query.offset+=64;memmove(g_page_sizes,g_page_sizes+1,3*sizeof(int));g_pages--;}
				memcpy(g_query.items+g_query.count,g_lazy.items,g_lazy.count*sizeof(g_lazy.items[0]));
				g_query.count+=g_lazy.count;g_page_sizes[g_pages++]=g_lazy.count;g_query.has_more=g_lazy.has_more;
			} else {
				if(g_pages==4){g_query.count-=g_page_sizes[3];g_pages--;g_query.has_more=1;}
				memmove(g_query.items+g_lazy.count,g_query.items,g_query.count*sizeof(g_query.items[0]));
				memcpy(g_query.items,g_lazy.items,g_lazy.count*sizeof(g_lazy.items[0]));g_query.count+=g_lazy.count;
				memmove(g_page_sizes+1,g_page_sizes,g_pages*sizeof(int));g_page_sizes[0]=g_lazy.count;g_pages++;
				g_selected+=g_lazy.count;g_top+=g_lazy.count;ui_theme_page_rebase(UI_SCENE_HOME,g_top-g_lazy.count,g_top);g_query.offset-=64;
			}
			if(g_selected<0)g_selected=0;
			if(g_top<0)g_top=0;
		} else if(g_lazy_generation==g_context_generation && g_lazy_direction!=2) g_lazy_failed_offset=(int)g_lazy.offset;
	}
    if(g_lazy_thread>=0)return;
    if(g_refresh_pending) {
        g_lazy=g_query;g_lazy.count=0;g_lazy.cancel=0;g_lazy.cancel_parent=NULL;
        g_lazy_direction=g_refresh_pending==2?2:0;g_refresh_pending=0;
        g_lazy_done=0;g_lazy_generation=g_context_generation;
        g_lazy_thread=sceKernelCreateThread("VmdCachedRefresh",lazy_worker,0x10000100,0x40000,0,0,NULL);
        if(g_lazy_thread>=0&&sceKernelStartThread(g_lazy_thread,0,NULL)<0){sceKernelDeleteThread(g_lazy_thread);g_lazy_thread=-1;}
        return;
    }
	if((!g_catalog && !g_episodes) || g_lazy_thread>=0)return;
	int direction=g_query.has_more && g_selected+rows*2>=g_query.count ? 1 :
	    g_query.offset>0 && g_selected<3 ? -1 : 0;
	if(!direction)return;
	memset(&g_lazy,0,sizeof(g_lazy));g_lazy.kind=g_query.kind;
	g_lazy.browse=g_query.browse;g_lazy.search_field=g_query.search_field;
	memcpy(g_lazy.country,g_query.country,sizeof(g_lazy.country));snprintf(g_lazy.query,sizeof(g_lazy.query),"%s",g_query.query);
	g_lazy.offset=direction>0?g_query.offset+g_pages*64:g_query.offset-64;
	if((int)g_lazy.offset==g_lazy_failed_offset)return;
	g_lazy_direction=direction;g_lazy_done=0;g_lazy_generation=g_context_generation;
	g_lazy_thread=sceKernelCreateThread("VmdCatalogPage",lazy_worker,0x10000100,0x40000,0,0,NULL);
	if(g_lazy_thread>=0 && sceKernelStartThread(g_lazy_thread,0,NULL)<0){sceKernelDeleteThread(g_lazy_thread);g_lazy_thread=-1;g_query.has_more=0;}
	if(g_lazy_thread<0)g_lazy_failed_offset=(int)g_lazy.offset;
}

static int list_count(void) {
	return g_query.count + (g_query.kind == VT_ONLINE_COUNTRY) + !!THEMED_OFFLINE;
}

static UiPageView online_page(void) {
    char key[1536];
    snprintf(key,sizeof(key),"%d:%d:%d:%d:%d:%s:%s",g_tab,g_menu,g_catalog,g_episodes,
        g_query.kind,g_episodes?g_feed_title:g_query.country,g_query.query);
    return ui_theme_page(UI_SCENE_HOME,"online",key,g_top,list_count(),
        CATALOG_COLUMNS,CATALOG_ROWS*CATALOG_COLUMNS,CATALOG_STEP);
}
static UiThemeRect online_item_rect(int slot){
 if(THEMED)return ui_theme_home_item_rect(ui_theme_id(),slot,ui_mini_player_top()<UI_CONTENT_BOTTOM?ui_mini_player_top():UI_CONTENT_BOTTOM);
 int columns=CATALOG_COLUMNS,width=PODCAST_HOME?vt_catalog_width(2):vt_catalog_width(columns);
 int row=slot/columns,col=slot%columns;if(col<0){col+=columns;row--;}
 return ui_theme_page_rect(UI_SCENE_HOME,(UiThemeRect){UI_CONTENT_X+col*(width+UI_CONTENT_GAP),LIST_Y+row*CATALOG_STEP,width,CATALOG_HEIGHT});
}

static int remember_list(void) {
    if(g_return_valid) {
        if(g_history_count==ONLINE_HISTORY_MAX)return -1;
        struct OnlineParent *parent=malloc(sizeof(*parent));
        if(!parent)return -1;
        parent->query=g_return_query;parent->selected=g_return_selected;parent->top=g_return_top;
        parent->catalog=g_return_catalog;parent->pages=g_return_pages;
        memcpy(parent->sizes,g_return_sizes,sizeof(parent->sizes));
        snprintf(parent->country,sizeof(parent->country),"%s",g_return_country);
        snprintf(parent->feed,sizeof(parent->feed),"%s",g_return_feed);
        g_history[g_history_count++]=parent;
    }
    lazy_stop();cover_stop();
	g_return_query = g_query;
	memcpy(g_return_sizes,g_page_sizes,sizeof(g_page_sizes));g_return_pages=g_pages;
	g_return_selected = g_selected; g_return_top = g_top;
	g_return_catalog = g_catalog; g_return_valid = 1;
    snprintf(g_return_country,sizeof(g_return_country),"%s",g_country_name);
    snprintf(g_return_feed,sizeof(g_return_feed),"%s",g_feed_title);
    return 0;
}

static void load_saved(void) {
    cache_home();clear_history();g_saved_valid=g_saved_changed=0;g_country_name[0]=g_feed_title[0]=0;
	g_menu=0;
	lazy_stop();cover_stop();g_pages=1;g_lazy_failed_offset=-1;
	int cached_slot=g_tab==VT_ONLINE_FEED;
    if(g_home_cache[cached_slot].valid) {
        g_query.count=g_home_cache[cached_slot].count;
        memcpy(g_query.items,g_home_cache[cached_slot].items,g_query.count*sizeof(VtOnlineItem));
    } else g_query.count=vt_online_load_saved(g_query.items, VT_ONLINE_MAX);
    if(g_query.count<0)g_query.count=0;
	int used = 0;
	for (int i = 0; i < g_query.count; i++)
		if (g_query.items[i].kind == g_tab) g_query.items[used++] = g_query.items[i];
	g_query.count = used;
	g_query.kind = g_tab;
	g_query.offset = 0; g_query.has_more = 0;g_query.query[0]=0;g_query.browse=g_query.search_field=0;g_query.country[0]=0;
	g_catalog = g_return_valid = 0;
	g_selected = g_top = g_episodes = 0;
    g_query.cancel=0;g_query.cancel_parent=NULL;g_home_list_loaded=1;cache_home();g_refresh_pending=2;
}

static void restore_list(void) {
	lazy_stop(); cover_stop(); g_lazy_failed_offset = -1;
	g_query = g_return_query;
	g_selected = g_return_selected; g_top = g_return_top;
	memcpy(g_page_sizes, g_return_sizes, sizeof(g_page_sizes));
	g_pages = g_return_pages; g_catalog = g_return_catalog;
	g_episodes = g_query.kind == VT_ONLINE_EPISODE;
	g_menu = g_query.kind == VT_ONLINE_MENU;
    snprintf(g_country_name,sizeof(g_country_name),"%s",g_return_country);
    snprintf(g_feed_title,sizeof(g_feed_title),"%s",g_return_feed);
    g_query.cancel=0;g_query.cancel_parent=NULL;
    g_return_valid=0;
    if(g_history_count) {
        struct OnlineParent *parent=g_history[--g_history_count];
        g_return_query=parent->query;g_return_selected=parent->selected;g_return_top=parent->top;
        g_return_catalog=parent->catalog;g_return_pages=parent->pages;
        memcpy(g_return_sizes,parent->sizes,sizeof(g_return_sizes));
        snprintf(g_return_country,sizeof(g_return_country),"%s",parent->country);
        snprintf(g_return_feed,sizeof(g_return_feed),"%s",parent->feed);
        g_return_valid=1;free(parent);
    }
    /* A favourite removed from a catalogue must not reappear in a cached parent. */
    if(saved_list() && g_saved_changed && g_saved_valid) {
        int kept=0;
        for(int i=0;i<g_query.count;i++)if(favorite_index(&g_query.items[i])>=0)g_query.items[kept++]=g_query.items[i];
        g_query.count=kept;
        if(g_selected>=kept)g_selected=kept?kept-1:0;
        if(g_top>g_selected)g_top=g_selected;
    }
}

static void playback_failed(void) {
	/* A failed episode must not strand Home on its episode action toolbar. */
	if (!g_home || !g_episodes) return;
	if (g_return_valid && !g_return_catalog &&
	    g_return_query.kind == VT_ONLINE_FEED) restore_list();
	else load_saved();
}

static int radio_fullscreen(uint64_t position, void *ctx) {
	(void)position; (void)ctx;
	VtBackgroundPlaybackSnapshot snapshot;
	vt_background_playback_snapshot(&snapshot);
	int action = ui_music_player_run(snapshot.thumbnail_url, snapshot.is_live ? "" : "Podcast",
	    snapshot.is_live ? "MP3 / Live" : "Audio", 0);
	if (action == UI_MUSIC_PLAYER_MINIMIZE) return 0;
	if (action >= UI_MUSIC_PLAYER_SECTION_BASE) return action;
	vt_background_playback_snapshot(&snapshot);
	int failed = snapshot.state == VT_BACKGROUND_ERROR;
	vt_background_playback_stop();
	if (failed) playback_failed();
	return failed ? -1 : 0;
}

static void reset_input(SceCtrlData *previous, UiNavRepeat *repeat) {
	ui_touch_reset();
	sceCtrlPeekBufferPositive(0, previous, 1);
	ui_nav_repeat_reset(repeat);
}

static int fetch_and_cache(void *ctx) {
    VtOnlineQuery *query=ctx;
    int result=vt_online_fetch_query(query);
	if (result >= 0 && !query->cancel) {
		vt_online_query_cache_write(query);
		library_query_cache(query);
	}
	return result;
}
static int fetch_page(int kind,const char *text,unsigned offset,int remember_parent) {
    VtOnlineQuery *next=calloc(1,sizeof(*next));if(!next)return -1;
    next->kind=kind;next->offset=offset;next->browse=g_query.browse;next->search_field=g_query.search_field;
    memcpy(next->country,g_query.country,sizeof(next->country));
    snprintf(next->query,sizeof(next->query),"%s",text);
    int cached=vt_online_query_cache_read(next)==0;
    int ret=cached?0:ui_loading_run(TR(ONLINE_LOADING),fetch_and_cache,next,&next->cancel,NULL,NULL);
    if(next->cancel || (kind==VT_ONLINE_EPISODE&&!offset&&!next->count))ret=-1;
    if(ret<0){
        if(!next->cancel)ui_message_show(TR(ONLINE_FAILED),TR(ONLINE_RETRY),3000);
        log_save(VITAMEDIADECK_SESSION_LOG_PATH);free(next);return ret;
    }
    if(remember_parent) {
        if(remember_list()<0){free(next);ui_message_show(TR(ONLINE_FAILED),TR(ONLINE_RETRY),2200);return -1;}
    } else {lazy_stop();cover_stop();}
    g_query=*next;g_query.cancel_parent=NULL;free(next);g_menu=0;
    g_selected=g_top=0;g_pages=1;g_page_sizes[0]=g_query.count;
    g_catalog=kind!=VT_ONLINE_EPISODE;g_episodes=kind==VT_ONLINE_EPISODE;
    if(cached)g_refresh_pending=1;
    return ret;
}


static void browse_menu(void) {
	clear_history();lazy_stop();cover_stop();g_country_name[0]=g_feed_title[0]=0;g_menu=1;g_episodes=g_catalog=0;
	g_query.count=0;g_query.has_more=0;g_query.offset=0;g_query.kind=VT_ONLINE_MENU;
	g_query.browse=0;g_query.search_field=0;g_query.query[0]=g_query.country[0]=0;g_selected=g_top=0;
	const VtStringId labels[]={VT_STR_ONLINE_POPULAR,VT_STR_ONLINE_REGIONS,VT_STR_ONLINE_GENRES,VT_STR_ONLINE_FAVORITES};
	for(int i=0;i<4;i++) {
		if(i==2&&g_tab!=VT_ONLINE_RADIO)continue;
		VtOnlineItem *item=&g_query.items[g_query.count++];memset(item,0,sizeof(*item));
		item->kind=VT_ONLINE_MENU;snprintf(item->id,sizeof(item->id),"%d",i);
		snprintf(item->title,sizeof(item->title),"%s",vt_i18n_str(labels[i]));
		const VtStringId descriptions[]={VT_STR_ONLINE_POPULAR_DETAIL,VT_STR_ONLINE_REGIONS_DETAIL,VT_STR_ONLINE_GENRES_DETAIL,VT_STR_ONLINE_SAVED_DETAIL};
		snprintf(item->detail,sizeof(item->detail),"%s",vt_i18n_str(descriptions[i]));
	}
}

static void select_country(const char *code, const char *name) {
    int remember_parent = g_query.kind == VT_ONLINE_COUNTRY;
    char old_country[3];memcpy(old_country,g_query.country,sizeof(old_country));
    int old_browse=g_query.browse,old_field=g_query.search_field;
    snprintf(g_query.country,sizeof(g_query.country),"%.2s",code);
    g_query.browse=1;g_query.search_field=0;
    if(fetch_page(g_tab==VT_ONLINE_RADIO?VT_ONLINE_RADIO:VT_ONLINE_SHOW,"",0,remember_parent)>=0) {
        if(remember_parent) {
            memcpy(g_return_query.country,old_country,sizeof(old_country));
            g_return_query.browse=old_browse;g_return_query.search_field=old_field;
        }
        snprintf(g_country_name,sizeof(g_country_name),"%s",name);
    } else {memcpy(g_query.country,old_country,sizeof(old_country));g_query.browse=old_browse;g_query.search_field=old_field;}
}

static int toggle_favorite(void) {
    if(g_selected<0 || g_selected>=g_query.count || !can_favorite(&g_query.items[g_selected]))return 0;
    VtOnlineItem item=g_query.items[g_selected];
    int found=favorite_index(&item),removing=saved_list()||found>=0;
    if(removing && found>=0){item.kind=g_saved_keys[found].kind;snprintf(item.url,sizeof(item.url),"%s",g_saved_keys[found].url);}
    if(!removing && item.kind==VT_ONLINE_SHOW) {
        g_action_cancel=0;
        if(ui_loading_run(TR(ONLINE_LOADING),show_loading,&item,&g_action_cancel,NULL,NULL)<0)return -1;
    }
    int result=removing?vt_online_remove(item.kind,item.url):vt_online_save(&item);
    if(result<0){ui_message_show(TR(ONLINE_FAILED),removing?TR(ONLINE_REMOVE_FAILED):TR(ONLINE_SAVE_FAILED),2600);return -1;}
    lazy_stop();cover_stop();g_saved_valid=0;g_saved_changed=1;refresh_favorites();
    g_home_cache[0].valid=g_home_cache[1].valid=0;
    if(removing && saved_list()) {
        memmove(g_query.items+g_selected,g_query.items+g_selected+1,(g_query.count-g_selected-1)*sizeof(g_query.items[0]));
        g_query.count--;
        if(g_selected>=g_query.count)g_selected=g_query.count?g_query.count-1:0;
        if(g_top>g_selected)g_top=g_selected;
        cache_home();
    }
    ui_message_show_success(removing?TR(ONLINE_REMOVED):TR(ONLINE_SAVED),item.title,1100);
    return 1;
}

static const char *online_content_hint(void) {
    return g_episodes?TR(ONLINE_EPISODE_HINT):g_menu || g_query.kind==VT_ONLINE_COUNTRY || g_query.kind==VT_ONLINE_TAG?TR(UI_OPEN_HINT):TR(ONLINE_FAVORITE_HINT);
}

static void draw_provider_tabs(int focused,UiFocusMotion *motion) {
    ui_action_button(UI_CONTENT_X,UI_TABS_Y,195,40,VT_THEME_SURFACE,"",TR(ONLINE_RADIO),0);
    ui_action_button(221,UI_TABS_Y,195,40,VT_THEME_SURFACE,"",TR(HOME_PODCAST),0);
    int x=g_tab==VT_ONLINE_RADIO?UI_CONTENT_X:221;
    vita2d_draw_rectangle(x,UI_TABS_BOTTOM-2,195,2,VT_THEME_SIGNAL_BRIGHT);
    if(focused){ui_focus_motion_tick(motion,x,UI_TABS_Y,195,40);ui_focus_outline_draw(motion);}
    ui_action_button(424,UI_TABS_Y,136,40,VT_THEME_SURFACE,"O",TR(UI_BACK),0);
    ui_action_button(756,UI_TABS_Y,186,40,VT_THEME_SURFACE,"R1",TR(UI_OPTIONS),0);
}

static vita2d_texture *online_artwork(const VtOnlineItem *item) {
    if(!item)return NULL;
    if(item->kind==VT_ONLINE_COUNTRY)return vt_country_flag_get(item->id);
    return item->artwork_path[0]&&strcmp(item->artwork_path,"-")?vt_artwork_thumbnail_get(item->artwork_path):NULL;
}

static void draw_themed_online(int focus_sections,UiFocusMotion *focus,UiSectionsSidebar *sidebar,UiContextMenu *options){
 uint64_t frame_started_us=sceKernelGetProcessTimeWide();
 int bottom=ui_mini_player_top()<UI_CONTENT_BOTTOM?ui_mini_player_top():UI_CONTENT_BOTTOM;
 int page_focus=!ui_mini_player_input_locked()&&!sidebar->open&&sidebar->animation<=.01f&&!options->open&&options->animation<=.01f;
 const char *context=g_menu?TR(ONLINE_BROWSE):g_episodes?g_feed_title:
     !g_catalog?TR(ONLINE_FAVORITES):g_query.kind==VT_ONLINE_COUNTRY?TR(ONLINE_REGIONS):
     g_query.kind==VT_ONLINE_TAG?TR(ONLINE_GENRES):TR(ONLINE_CATALOG);
 vita2d_start_drawing();vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD_LIGHT);
 ui_brand_set_loading(g_lazy_thread>=0||g_refresh_pending);
 if(g_home){ui_brand_draw_header(NULL);ui_home_tabs_set_expanded(1);
  ui_home_tabs_draw_with_focus(focus_sections&&page_focus,focus);
 }else{ui_brand_draw_breadcrumbs(g_tab==VT_ONLINE_RADIO?TR(ONLINE_RADIO):TR(HOME_PODCAST),context,g_episodes?g_feed_title:g_country_name);
  draw_provider_tabs(focus_sections&&page_focus,focus);
 }
 UiPageView page=online_page();
 if(g_home)ui_theme_home_heading(ui_home_tab_label(),context,bottom);
 else {
  const char *name=g_tab==VT_ONLINE_RADIO?TR(ONLINE_RADIO):TR(HOME_PODCAST);
  ui_theme_scene_begin(UI_SCENE_HOME,name);
  ui_text_line(HOME_LAYOUT.x,128,942-HOME_LAYOUT.x,name,VT_THEME_TEXT,UI_FONT_BODY);
  ui_text_line(HOME_LAYOUT.x,HOME_LAYOUT.y-14,942-HOME_LAYOUT.x,g_episodes?g_feed_title:g_country_name[0]?g_country_name:context,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }
 vita2d_set_clip_rectangle(0,HOME_LAYOUT.y-4,960,bottom);vita2d_enable_clipping();
 for(int index=page.first;index<page.end;index++){
  int slot=index-g_top;const char *title,*detail="";char metadata[256],favorite_title[192];
  if(index<g_query.count){VtOnlineItem *item=&g_query.items[index];title=item->title;detail=item->detail;
   if(item->kind==VT_ONLINE_EPISODE){char date[16],duration[24];episode_labels(item,date,duration);
    snprintf(metadata,sizeof(metadata),"%s  /  %s",date,duration);detail=metadata;}
  }else if(THEMED_OFFLINE){title=TR(HOME_PODCAST_FILES);detail="ux0:podcast / ux0:podcasts";}
  else{title=g_tab==VT_ONLINE_RADIO?TR(ONLINE_WORLD):TR(ONLINE_US_STORE);}
  if(index<g_query.count && can_favorite(&g_query.items[index]) && (saved_list()||favorite_index(&g_query.items[index])>=0)) {
   snprintf(favorite_title,sizeof(favorite_title),"* %s",title);title=favorite_title;
  }
  int selected=index==g_selected&&!focus_sections&&page_focus;
  VtOnlineItem *item=index<g_query.count?&g_query.items[index]:NULL;
  int media=item&&(item->kind==VT_ONLINE_RADIO||item->kind==VT_ONLINE_FEED||item->kind==VT_ONLINE_SHOW||item->kind==VT_ONLINE_EPISODE||item->kind==VT_ONLINE_COUNTRY);
  if(media)ui_theme_home_row_art(slot,index,title,detail,index==g_selected,!focus_sections&&page_focus,bottom,online_artwork(item),item->kind==VT_ONLINE_COUNTRY?"--":item->kind==VT_ONLINE_RADIO?"FM":"PD");
  else ui_theme_home_row(slot,index,title,detail,index==g_selected,!focus_sections&&page_focus,bottom);
  if(selected){UiThemeRect target=online_item_rect(slot);ui_focus_motion_tick(focus,target.x,target.y,target.w,target.h);}
 }
 vita2d_disable_clipping();
 ui_theme_page_finish(UI_SCENE_HOME,HOME_LAYOUT.x,HOME_LAYOUT.y,HOME_LAYOUT.w,bottom);
 if(!list_count())ui_text_line(HOME_LAYOUT.x,LIST_Y+30,HOME_LAYOUT.w,g_home?TR(ONLINE_HOME_EMPTY):TR(ONLINE_PROVIDER_EMPTY),VT_THEME_TEXT_MUTED,UI_FONT_BODY);
 if(ui_theme_id()==VT_UI_THEME_APERTURE){
  vita2d_texture *art=NULL;const char *name=NULL;
  if(g_selected<g_query.count){VtOnlineItem *item=&g_query.items[g_selected];name=item->title;
   if(g_query.kind==VT_ONLINE_COUNTRY)art=vt_country_flag_get(item->id);
   else if(item->artwork_path[0]&&strcmp(item->artwork_path,"-"))art=vt_artwork_thumbnail_get(item->artwork_path);}
  ui_theme_home_preview(art,name,bottom,0);
 }
 if(g_home)ui_theme_home_footer_with_focus(vt_i18n_str(g_episodes?VT_STR_ONLINE_EPISODE_HINT:VT_STR_ONLINE_FAVORITE_HINT),bottom,focus_sections);
 else ui_text_line(18,bottom-12,924,focus_sections?TR(ONLINE_PROVIDER_TABS_HINT):online_content_hint(),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 ui_theme_home_position(g_selected,list_count(),bottom);
 if(!focus_sections&&page_focus&&list_count())ui_focus_outline_draw(focus);
 ui_mini_player_draw();UiContextAction items[8];int n=online_options(items,0);
 ui_context_menu_draw(options,TR(UI_OPTIONS),items,n);
 if(sidebar->animation>.01f)ui_sections_sidebar_draw(sidebar);
 vita2d_end_drawing();vita2d_wait_rendering_done();vita2d_swap_buffers();ui_frame_pace(frame_started_us);
}

int ui_online_audio_screen(void) {
 g_saved_valid=0;
 if(THEMED){
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  ui_home_tabs_set_expanded(1);
  if(!g_home)ui_home_tab_set(g_tab==VT_ONLINE_RADIO?HOME_RADIO:HOME_PODCAST);
 }
	if(g_home) {
		int requested_tab=ui_home_tab()==HOME_RADIO?VT_ONLINE_RADIO:VT_ONLINE_FEED;
		if(g_tab!=requested_tab||g_mode_changed){g_tab=requested_tab;load_saved();}
	} else if(g_mode_changed||!g_initialized)browse_menu();
	g_mode_changed=0;g_initialized=1;
	int focus_tabs = g_home ? 1 : 0;
	int offline_focus = 0;
	UiContextMenu options={0};
	UiFocusMotion focus_motion={0};
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, g_home?UI_SECTION_LOCAL_MEDIA:UI_SECTION_ONLINE_AUDIO);
	SceCtrlData previous = {0};
	UiNavRepeat repeat;
	reset_input(&previous, &repeat);
	for (;;) {
        uint64_t frame_started_us=sceKernelGetProcessTimeWide();
		ui_mini_player_pump();
		SceCtrlData controls = {0};
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		ui_mini_player_handle_buttons(&pressed);
		UiTouchEvent touch;
		unsigned flags = ui_touch_poll(&touch);
		if (ui_mini_player_input_locked()) {
			pressed = flags = 0;
			controls.buttons = 0; controls.lx = controls.ly = controls.rx = controls.ry = 128;
		}
		if (ui_brand_sections_hit(flags, touch.x, touch.y)) {
			pressed |= SCE_CTRL_LTRIGGER; flags = 0;
		}
		if((!THEMED || !g_home) && (flags&UI_TOUCH_EVENT_TAP) && ui_touch_hit_rect(touch.x,touch.y,756,OPTIONS_Y,186,40)) {
			pressed|=SCE_CTRL_RTRIGGER;flags=0;
		}
		if(pressed&SCE_CTRL_RTRIGGER)sidebar.open=0;
		UiContextAction option_items[8];int option_count=online_options(option_items,offline_focus);
		int option_action=ui_context_menu_input(&options,option_items,option_count,&pressed,controls.buttons,controls.ly,&flags,&touch);
		ui_context_menu_tick(&options);
		int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
		    controls.buttons, controls.ly);
		if (sidebar.open) {
			int selected = ui_sections_sidebar_handle_touch(&sidebar, flags, touch.x, touch.y);
			if (selected != UI_SECTION_NONE) section = selected;
			flags = 0;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) return online_leave(section);
		int columns = CATALOG_COLUMNS;
		int rows = CATALOG_ROWS;
		lazy_tick(rows * columns);
		g_top = vt_catalog_top(g_selected, g_top, columns, rows);
		cover_tick(rows * columns);vt_video_thumbnail_resume();vt_video_thumbnail_pump();
		if (!sidebar.open && sidebar.animation <= 0.01f &&
		    ((!options.open && options.animation<=.01f) || option_action>=0)) {
			if(option_action==ONLINE_ACTION_OFFLINE) {ui_home_podcast_set_offline(1);return online_leave(UI_SECTION_LOCAL_MEDIA);}
			if(option_action==ONLINE_ACTION_BROWSE) {
				if(g_home) {ui_online_audio_set_home(0);return online_leave(UI_SECTION_ONLINE_AUDIO);}
				browse_menu();focus_tabs=0;reset_input(&previous,&repeat);continue;
			}
			if(option_action==ONLINE_ACTION_SAVED) {load_saved();focus_tabs=0;reset_input(&previous,&repeat);continue;}
			if(option_action==ONLINE_ACTION_REFRESH) {
				if(g_catalog||g_episodes)fetch_page(g_query.kind,g_query.query,0,0);else load_saved();
				reset_input(&previous,&repeat);continue;
			}
			if(option_action>=0) {pressed|=(unsigned)option_action;focus_tabs=offline_focus=0;}
			if (ui_mini_player_handle_touch(flags, &touch)) flags = 0;
			if((!THEMED||(touch.down_x>=HOME_LAYOUT.x&&touch.down_x<HOME_LAYOUT.x+HOME_LAYOUT.columns*(HOME_LAYOUT.w+12)))&&
       (flags&UI_TOUCH_EVENT_UP)&&!(flags&UI_TOUCH_EVENT_TAP)&&touch.down_y>=LIST_Y&&touch.down_y<LIST_Y+rows*CATALOG_STEP) {
				int delta=(touch.down_y-touch.y)/CATALOG_STEP;
				g_selected+=delta*columns;focus_tabs=0;
				if(g_selected<0)g_selected=0;
				if(g_selected>=g_query.count)g_selected=g_query.count?g_query.count-1:0;
			}
			if (flags & UI_TOUCH_EVENT_TAP) {
				if(PODCAST_HOME && ui_touch_hit_rect(touch.x,touch.y,PODCAST_X,LIST_Y,vt_catalog_width(2),rows*CATALOG_STEP-8)) {
					ui_home_podcast_set_offline(1);return online_leave(UI_SECTION_LOCAL_MEDIA);
				}
				if(!g_home && !g_episodes && ui_touch_hit_rect(touch.x,touch.y,568,UI_TABS_Y,180,40))pressed|=SCE_CTRL_TRIANGLE;
				if(!g_home && ui_touch_hit_rect(touch.x,touch.y,424,UI_TABS_Y,136,40))pressed|=SCE_CTRL_CIRCLE;
                UiPageView visible=online_page();
                for(int index=visible.first;index<visible.end;index++){
     UiThemeRect hit=online_item_rect(index-g_top);
     if(touch.y>=LIST_Y && touch.y<CATALOG_BOTTOM && ui_touch_hit_rect(touch.x,touch.y,hit.x,hit.y,hit.w,hit.h)){
      focus_tabs=offline_focus=0;g_selected=index;pressed|=SCE_CTRL_CROSS;
     }
    }
			}
			unsigned nav=0;
   if(THEMED && g_home){
    UiHomeControlSample input={.pressed=pressed,.held=controls.buttons,.touch_flags=flags,
     .lx=controls.lx,.ly=controls.ly,.rx=controls.rx,.ry=controls.ry,.selected=g_selected,
     .count=list_count(),.bottom=ui_mini_player_top()<UI_CONTENT_BOTTOM?ui_mini_player_top():UI_CONTENT_BOTTOM,
     .focus_sections=focus_tabs,.blocked=ui_mini_player_input_locked(),.touch=&touch};
    UiHomeControlResult routed=ui_home_controls_update(&input);
    pressed=routed.pressed;nav=routed.content_nav;focus_tabs=routed.focus_sections;
    if(routed.sections_changed){
     if(!ui_home_tab_online())return online_leave(UI_SECTION_LOCAL_MEDIA);
     g_home=1;g_tab=ui_home_tab()==HOME_RADIO?VT_ONLINE_RADIO:VT_ONLINE_FEED;
     load_saved();reset_input(&previous,&repeat);
    }
   }else{
    unsigned tab_nav=ui_home_tabs_navigation(pressed,focus_tabs?controls.buttons:0,focus_tabs?controls.lx:128,128);
    if(g_home&&ui_home_tabs_input(tab_nav,flags,&touch,focus_tabs)){
     focus_tabs=1;
     if(!ui_home_tab_online())return online_leave(UI_SECTION_LOCAL_MEDIA);
     g_tab=ui_home_tab()==HOME_RADIO?VT_ONLINE_RADIO:VT_ONLINE_FEED;
     load_saved();reset_input(&previous,&repeat);
    }
    if(!g_home){
     int next=g_tab;
     if(focus_tabs&&(tab_nav&SCE_CTRL_LEFT))next=VT_ONLINE_RADIO;
     if(focus_tabs&&(tab_nav&SCE_CTRL_RIGHT))next=VT_ONLINE_FEED;
     if((flags&UI_TOUCH_EVENT_TAP)&&ui_touch_hit_rect(touch.x,touch.y,UI_CONTENT_X,UI_TABS_Y,195,40)){next=VT_ONLINE_RADIO;focus_tabs=1;}
     if((flags&UI_TOUCH_EVENT_TAP)&&ui_touch_hit_rect(touch.x,touch.y,221,UI_TABS_Y,195,40)){next=VT_ONLINE_FEED;focus_tabs=1;}
     if(next!=g_tab){g_tab=next;browse_menu();reset_input(&previous,&repeat);}
    }
    if(focus_tabs&&(pressed&(SCE_CTRL_DOWN|SCE_CTRL_CROSS))){focus_tabs=0;pressed&=~(SCE_CTRL_DOWN|SCE_CTRL_CROSS);controls.buttons&=~SCE_CTRL_DOWN;}
    else if(!focus_tabs&&g_selected<columns&&(pressed&SCE_CTRL_UP)){focus_tabs=1;pressed&=~SCE_CTRL_UP;}
    nav=ui_nav_repeat_update(&repeat,pressed,controls.buttons,controls.lx,controls.ly,SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
   }
			if(PODCAST_HOME && !focus_tabs) {
				if(nav&SCE_CTRL_RIGHT)offline_focus=1;
				if(nav&SCE_CTRL_LEFT)offline_focus=0;
				if(offline_focus && (pressed&SCE_CTRL_CROSS)) {ui_home_podcast_set_offline(1);return online_leave(UI_SECTION_LOCAL_MEDIA);}
			}
			if (!focus_tabs && (nav & SCE_CTRL_UP) && g_selected >= columns) g_selected-=columns;
			if (!focus_tabs && (nav & SCE_CTRL_DOWN) && (g_selected/columns+1)*columns < list_count()) {
				g_selected+=columns; if(g_selected>=list_count())g_selected=list_count()-1;
			}
			if (!focus_tabs && (nav & SCE_CTRL_LEFT) && g_selected%columns) g_selected--;
			if (!focus_tabs && (nav & SCE_CTRL_RIGHT) && g_selected%columns+1<columns && g_selected+1<list_count()) g_selected++;
			if (pressed & SCE_CTRL_CIRCLE) {
				if (g_return_valid) {
					restore_list();
                }else if(!g_home && !g_menu){browse_menu();focus_tabs=0;}
                else if(!g_home){ui_online_audio_set_home(1);return online_leave(UI_SECTION_LOCAL_MEDIA);}
                else if(g_episodes){load_saved();focus_tabs=0;}
                else focus_tabs=1;
				reset_input(&previous, &repeat);pressed=0;
			}
			if (!g_home && (pressed & SCE_CTRL_START) && g_query.kind == VT_ONLINE_COUNTRY) {
				select_country("", ""); reset_input(&previous, &repeat);
				pressed = 0;
			}
			if (!g_home && !g_episodes && (pressed & SCE_CTRL_TRIANGLE)) {
				char text[128] = "";
				int kind = g_query.kind == VT_ONLINE_COUNTRY ? VT_ONLINE_COUNTRY : g_tab;
				if (ui_text_input(vt_i18n_str(kind == VT_ONLINE_FEED ? VT_STR_ONLINE_DISCOVER_PROMPT :
				    kind == VT_ONLINE_COUNTRY ? VT_STR_ONLINE_COUNTRY_SEARCH : VT_STR_ONLINE_SEARCH), "", text, sizeof(text)) > 0) {
                    int old_browse=g_query.browse,old_field=g_query.search_field;
                    g_query.browse=0;g_query.search_field=0;
                    if(fetch_page(kind,text,0,g_menu)<0){g_query.browse=old_browse;g_query.search_field=old_field;}
				}
				reset_input(&previous, &repeat);
			}
			if (!g_home && (pressed & SCE_CTRL_START) && !g_episodes && g_query.kind != VT_ONLINE_COUNTRY) {
				VtOnlineItem item = {.kind = g_tab};
				if (ui_text_input(vt_i18n_str(g_tab == VT_ONLINE_RADIO ? VT_STR_ONLINE_MP3_URL : VT_STR_ONLINE_RSS_URL),
				    "", item.url, sizeof(item.url)) > 0 && vt_online_url_valid(item.url) &&
				    ui_text_input(vt_i18n_str(VT_STR_ONLINE_NAME), "", item.title, sizeof(item.title)) > 0) {
					g_home_cache[0].valid=g_home_cache[1].valid=0;
					if (vt_online_save(&item) < 0)
						ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED), vt_i18n_str(VT_STR_ONLINE_SAVE_FAILED), 2600);
					load_saved();
				}
				reset_input(&previous, &repeat);
			}
			if (((pressed&SCE_CTRL_SELECT)||(g_episodes&&(pressed&SCE_CTRL_SQUARE))) && g_selected<g_query.count) {
				VtOnlineItem *item=&g_query.items[g_selected];char text[1536];
				snprintf(text,sizeof(text),"%s\n\n%s\n\n%s",item->author,item->detail,item->description);
				int result=ui_text_reader_run(item->title,text);
				if(result!=UI_SECTION_NONE)return online_leave(result);
				reset_input(&previous,&repeat);
			}
            if(!focus_tabs && !(PODCAST_HOME&&offline_focus) && (pressed&SCE_CTRL_SQUARE) && !g_episodes) {
                toggle_favorite();reset_input(&previous,&repeat);
            }
			if (((pressed & SCE_CTRL_CROSS) || (g_episodes && (pressed & SCE_CTRL_START))) && g_selected < list_count()) {
				if(THEMED_OFFLINE&&g_selected==g_query.count){ui_home_podcast_set_offline(1);return online_leave(UI_SECTION_LOCAL_MEDIA);}
				if (g_selected >= g_query.count) {
					int extra = g_selected - g_query.count;
					if (g_query.kind == VT_ONLINE_COUNTRY && extra == 0) {
						select_country("", "");
					} else {
						extra -= g_query.kind == VT_ONLINE_COUNTRY;
						unsigned offset = g_query.offset > 0 && extra == 0
						    ? g_query.offset - VT_ONLINE_MAX : g_query.offset + VT_ONLINE_MAX;
						fetch_page(g_query.kind, g_query.query, offset, 0);
					}
					reset_input(&previous, &repeat);
					continue;
				}
				VtOnlineItem item = g_query.items[g_selected];
				if(item.kind==VT_ONLINE_SHOW){g_action_cancel=0;
					if(ui_loading_run(vt_i18n_str(VT_STR_ONLINE_LOADING),show_loading,&item,&g_action_cancel,NULL,NULL)<0){ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED),vt_i18n_str(VT_STR_ONLINE_RETRY),2500);reset_input(&previous,&repeat);continue;}}
				if(item.kind==VT_ONLINE_MENU) {
					int action=atoi(item.id);g_query.browse=1;g_query.search_field=0;
					if(action==0)fetch_page(g_tab==VT_ONLINE_RADIO?VT_ONLINE_RADIO:VT_ONLINE_SHOW,"",0,1);
                    else if(action==1)fetch_page(VT_ONLINE_COUNTRY,"",0,1);
                    else if(action==2)fetch_page(VT_ONLINE_TAG,"",0,1);else load_saved();
				} else if(item.kind==VT_ONLINE_TAG) {
					g_query.browse=1;g_query.search_field=1;fetch_page(VT_ONLINE_RADIO,item.id,0,1);
				} else if (item.kind == VT_ONLINE_COUNTRY) {
					select_country(item.id, item.title);
				} else if (item.kind == VT_ONLINE_FEED) {
					if (fetch_page(VT_ONLINE_EPISODE, item.url, 0, 1) >= 0)
						snprintf(g_feed_title, sizeof(g_feed_title), "%s", item.title);
				} else if (item.kind == VT_ONLINE_RADIO || (item.kind == VT_ONLINE_EPISODE && (pressed & SCE_CTRL_CROSS))) {
                    lazy_stop();cover_stop();
                    /* Play immediately with the already cached cover. */
                    if(!strcmp(item.artwork_path,"-"))item.artwork_path[0]=0;
					VtLibraryItem library_item;
					vt_library_online(&item, g_query.query, &library_item);
					int result = ui_online_audio_open_library(&library_item);
					if (result >= UI_MUSIC_PLAYER_SECTION_BASE)
						return online_leave(result - UI_MUSIC_PLAYER_SECTION_BASE);
					if (result < 0)
						playback_failed();
				} else if (!vt_download_background_active()) {
					char folder[768];
					if (ui_folder_picker(NULL, folder, sizeof(folder))) {
						VtDownloadJob job;
						vt_download_job_init_url(&job, item.url);
						vt_download_job_set_destination(&job, folder);
						job.podcast = item;
						snprintf(job.podcast_show, sizeof(job.podcast_show), "%s", item.show_title);
						if (vt_download_background_start(&job) == 0)
							return online_leave(UI_SECTION_DOWNLOAD);
						ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED),
						                vt_i18n_str(VT_STR_ONLINE_RETRY), 2600);
					}
				} else
					return online_leave(UI_SECTION_DOWNLOAD);
				reset_input(&previous, &repeat);
			}
		}
		if (THEMED && (sidebar.open || sidebar.animation > .01f || options.open ||
		               options.animation > .01f || ui_mini_player_input_locked())) {
			UiHomeControlSample blocked = {.focus_sections = focus_tabs, .blocked = 1};
			ui_home_controls_update(&blocked);
		}
		columns = CATALOG_COLUMNS;
		rows = CATALOG_ROWS;
		g_top = vt_catalog_top(g_selected, g_top, columns, rows);
		if(THEMED){draw_themed_online(focus_tabs,&focus_motion,&sidebar,&options);continue;}
		vita2d_start_drawing(); vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_COLD_LIGHT);
		ui_brand_set_loading(g_lazy_thread>=0||g_refresh_pending);
		const char *section_name = g_tab==VT_ONLINE_RADIO ? TR(ONLINE_RADIO) : "Podcast";
		const char *context_name = g_menu ? TR(ONLINE_BROWSE) :
		    g_episodes ? TR(ONLINE_EPISODES) : !g_catalog ? TR(ONLINE_FAVORITES) :
		    g_query.kind==VT_ONLINE_COUNTRY ? TR(ONLINE_REGIONS) :
		    g_query.kind==VT_ONLINE_TAG ? TR(ONLINE_GENRES) :
		    g_country_name[0] ? g_country_name : TR(ONLINE_CATALOG);
		if (g_home) ui_brand_draw_home_header();
		else ui_brand_draw_breadcrumbs(section_name, context_name,
		    g_episodes ? g_feed_title : g_catalog && g_query.kind!=VT_ONLINE_COUNTRY ? g_query.query : NULL);
		int page_focus=!ui_mini_player_input_locked() && !sidebar.open && sidebar.animation<=.01f && !options.open && options.animation<=.01f;
		int draw_focus=0;
		if(g_home)ui_home_tabs_draw_with_focus(focus_tabs && page_focus,&focus_motion);
		else {
			ui_action_button(UI_CONTENT_X,UI_TABS_Y,195,40,VT_THEME_SURFACE,"",TR(ONLINE_RADIO),0);
			ui_action_button(221,UI_TABS_Y,195,40,VT_THEME_SURFACE,"","Podcast",0);
			if(focus_tabs && page_focus){ui_focus_motion_tick(&focus_motion,g_tab==VT_ONLINE_RADIO?UI_CONTENT_X:221,UI_TABS_Y,195,40);draw_focus=1;}
			vita2d_draw_rectangle(g_tab==VT_ONLINE_RADIO?UI_CONTENT_X:221,UI_TABS_BOTTOM-2,195,2,VT_THEME_SIGNAL_BRIGHT);
			if(!g_menu)ui_action_button(424,UI_TABS_Y,136,40,VT_THEME_SURFACE,"",TR(UI_BACK),0);
			if(!g_episodes)ui_action_button(568,UI_TABS_Y,180,40,VT_THEME_SURFACE,"",TR(ONLINE_SEARCH),0);
		}
		ui_action_button(756,OPTIONS_Y,186,40,VT_THEME_SURFACE,"R1",TR(UI_OPTIONS),0);
		vita2d_font *font = ui_runtime_font(UI_FONT_SMALL);
        UiPageView page=online_page();
        vita2d_set_clip_rectangle(0,LIST_Y-4,960,CATALOG_BOTTOM);vita2d_enable_clipping();
        for (int i=page.first;i<page.end;i++) {
            UiThemeRect rect=online_item_rect(i-g_top);
            int y=rect.y,width=rect.w,x=rect.x;
			int focused = i == g_selected && !focus_tabs && page_focus && !(PODCAST_HOME&&offline_focus);
			ui_panel(x, y, width, CATALOG_HEIGHT, VT_THEME_SURFACE, VT_THEME_BORDER_DIM, 0);
			if(focused){ui_focus_motion_tick(&focus_motion,x,y,width,CATALOG_HEIGHT);draw_focus=1;}
			char fitted[192],favorite_title[192];
			const char *title;
			int extra = i - g_query.count;
			if (i < g_query.count) title = g_query.items[i].title;
			else if (g_query.kind == VT_ONLINE_COUNTRY && extra == 0) title = g_tab==VT_ONLINE_RADIO?TR(ONLINE_WORLD):TR(ONLINE_US_STORE);
			else title = vt_i18n_str(g_query.offset > 0 && extra == (g_query.kind == VT_ONLINE_COUNTRY)
			    ? VT_STR_ONLINE_PREVIOUS : VT_STR_ONLINE_NEXT);
			if(i<g_query.count && can_favorite(&g_query.items[i]) && (saved_list()||favorite_index(&g_query.items[i])>=0)) {
                snprintf(favorite_title,sizeof(favorite_title),"* %s",title);title=favorite_title;
            }
            const char *art = i<g_query.count ? g_query.items[i].artwork_path : "";
			int art_size = CATALOG_COVERS ? CATALOG_HEIGHT-96 : columns==3 ? 40 : 56;
            if(art_size>width-20)art_size=width-20;
			vita2d_texture *cover=NULL;
			if(i<g_query.count && g_query.kind==VT_ONLINE_COUNTRY)cover=vt_country_flag_get(g_query.items[i].id);
			else if(art[0] && strcmp(art,"-"))cover=vt_artwork_thumbnail_get(art);
			if(g_query.kind!=VT_ONLINE_TAG && !g_menu)
				ui_artwork_tile(CATALOG_COVERS?x+(width-art_size)/2:x+10,CATALOG_COVERS?y+10:y+(CATALOG_HEIGHT-art_size)/2,art_size,cover,g_query.kind==VT_ONLINE_COUNTRY?"--":g_tab==VT_ONLINE_RADIO?"FM":"PD");
			int text_x = x + (CATALOG_COVERS || g_query.kind==VT_ONLINE_TAG || g_menu ? 14 : art_size+20);
			int text_width = x+width-12-text_x;
			if(i<g_query.count && g_query.items[i].kind==VT_ONLINE_EPISODE) {
				char date[16],duration[24];episode_labels(&g_query.items[i],date,duration);
				int date_width=ui_font_text_width(font,UI_FONT_SMALL,date);
				int duration_width=ui_font_text_width(font,UI_FONT_SMALL,duration);
				int reserve=date_width>duration_width?date_width:duration_width;
				if(reserve<128)reserve=128;
				text_width-=reserve+20;
				if(font) {
					ui_font_draw_text(font,x+width-16-date_width,y+28,VT_THEME_TEXT_MUTED,UI_FONT_SMALL,date);
					ui_font_draw_text(font,x+width-16-duration_width,y+54,VT_THEME_TEXT,UI_FONT_SMALL,duration);
				}
			}
			ui_font_fit_text(font, UI_FONT_SMALL, title, fitted, sizeof(fitted), text_width);
			int detail = i < g_query.count && g_query.items[i].detail[0];
			if (font) ui_font_draw_text(font, text_x, y + (CATALOG_COVERS ? CATALOG_HEIGHT-48 : detail ? 28 : 43), VT_THEME_TEXT, UI_FONT_SMALL, fitted);
			if (font && detail) {
				ui_font_fit_text(font, UI_FONT_SMALL, g_query.items[i].detail, fitted, sizeof(fitted), text_width);
				ui_font_draw_text(font, text_x, y + (CATALOG_COVERS ? CATALOG_HEIGHT-20 : 54), VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
			}
		}
        vita2d_disable_clipping();
        ui_theme_page_finish(UI_SCENE_HOME,UI_CONTENT_X,LIST_Y,UI_CONTENT_W,CATALOG_BOTTOM);
		if (font && !list_count()) {
			char fitted[192];ui_font_fit_text(font,UI_FONT_SMALL,vt_i18n_str(g_home?VT_STR_ONLINE_HOME_EMPTY:VT_STR_ONLINE_EMPTY),fitted,sizeof(fitted),PODCAST_HOME?vt_catalog_width(2)-28:UI_CONTENT_W-28);
			ui_font_draw_text(font, UI_CONTENT_X+14, LIST_Y + 30, VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
		}
		if(PODCAST_HOME && font) {
			ui_font_draw_text(font,UI_CONTENT_X,LIST_Y-8,VT_THEME_TEXT,UI_FONT_SMALL,"Online");
			ui_font_draw_text(font,PODCAST_X,LIST_Y-8,VT_THEME_TEXT,UI_FONT_SMALL,"Offline");
			int selected=offline_focus&&!focus_tabs&&page_focus;
			ui_panel(PODCAST_X,LIST_Y,vt_catalog_width(2),rows*CATALOG_STEP-8,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
			if(selected){ui_focus_motion_tick(&focus_motion,PODCAST_X,LIST_Y,vt_catalog_width(2),rows*CATALOG_STEP-8);draw_focus=1;}
			char fitted[192];ui_font_fit_text(font,UI_FONT_SMALL,vt_i18n_str(VT_STR_HOME_PODCAST_FILES),fitted,sizeof(fitted),vt_catalog_width(2)-28);
			ui_font_draw_text(font,PODCAST_X+14,LIST_Y+36,VT_THEME_TEXT,UI_FONT_SMALL,fitted);
			if(rows>=3) {
				ui_font_draw_text(font,PODCAST_X+14,LIST_Y+80,VT_THEME_TEXT_MUTED,UI_FONT_SMALL,"ux0:podcast / ux0:podcasts");
				ui_font_draw_text(font,PODCAST_X+14,LIST_Y+112,VT_THEME_TEXT_MUTED,UI_FONT_SMALL,"uma0:podcast / uma0:podcasts");
			}
			if(rows>=2)ui_action_button(PODCAST_X+14,LIST_Y+rows*CATALOG_STEP-66,vt_catalog_width(2)-28,44,VT_THEME_SURFACE,"X",vt_i18n_str(VT_STR_HOME_PODCAST_OPEN),0);
		}
		if (font) {
			char fitted[256];
			ui_font_fit_text(font, UI_FONT_SMALL, vt_i18n_str(g_home&&focus_tabs?VT_STR_HOME_HELP_LEGACY_TABS:!g_home&&focus_tabs?VT_STR_ONLINE_PROVIDER_TABS_HINT:g_episodes?VT_STR_ONLINE_EPISODE_HINT:g_menu||g_query.kind==VT_ONLINE_COUNTRY||g_query.kind==VT_ONLINE_TAG?VT_STR_UI_OPEN_HINT:VT_STR_ONLINE_FAVORITE_HINT),
			    fitted, sizeof(fitted), g_home?720:UI_CONTENT_W);
			ui_font_draw_text(font, UI_CONTENT_X, ui_mini_player_top() - 12, VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
		}
		ui_scroll_rail(948,LIST_Y,rows*CATALOG_STEP-8,g_top,rows*columns,list_count());
		if(draw_focus)ui_focus_outline_draw(&focus_motion);
		ui_mini_player_draw();
		option_count=online_options(option_items,offline_focus);
		ui_context_menu_draw(&options,TR(UI_OPTIONS),option_items,option_count);
		if (sidebar.animation > 0.01f) ui_sections_sidebar_draw(&sidebar);
		vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
		ui_frame_pace(frame_started_us);
	}
}

int ui_online_audio_open_library(const VtLibraryItem *v) {
	if (vt_background_playback_library_matches(v->id))
		return radio_fullscreen(0, NULL);
	vt_library_cache(v);
	vt_video_thumbnail_prepare_playback();
	int ret = v->provider == VT_LIBRARY_RADIO
	              ? vt_background_playback_prepare_radio(v->locator, v->title)
	              : vt_background_playback_prepare_podcast(v->locator, v->title);
	uint64_t resume = 0;
	if (!ret) {
		vt_background_playback_set_audio_details(v->artist, v->artwork);
		if (v->provider == VT_LIBRARY_PODCAST)
			resume = vt_background_playback_restore_podcast_history(v->history_id, v->duration_ms);
		vt_background_playback_set_library_id(v->id);
		ret = vt_background_playback_activate(resume);
	}
	if (!ret) {
		vt_library_started(v);
		vt_background_playback_set_fullscreen_resume(radio_fullscreen, NULL);
		return radio_fullscreen(0, NULL);
	}
	vt_background_playback_stop();
	ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED), vt_i18n_str(VT_STR_ONLINE_RETRY), 2800);
	return -1;
}
