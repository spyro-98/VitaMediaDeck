#include "ui/iptv_screen.h"
#include "library/catalog.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include "common/text_log.h"
#include "app_paths.h"
#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "media/hw_player_screen.h"
#include "media/video_thumbnail.h"
#include "network/iptv_session.h"
#include "network/iptv_catalog.h"
#include "network/dash_source.h"
#include "network/hls_source.h"
#include "network/iptv_probe.h"
#include <vita_https.h>
#include "network/http_audio_stream.h"
#include "network/online_audio.h"
#include "ui/brand.h"
#include "ui/layout.h"
#include "ui/components.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/qr_scanner.h"
#include "ui/theme.h"
#include "ui/touch.h"

/* Keep links in RAM unless the user explicitly saves or stars them. */
static char link[VT_IPTV_URL_MAX];
static char channel_title[160];
static char channel_agent[VT_IPTV_USER_AGENT_MAX];
static int mode;
static VtIptvCatalog catalog;
static int catalog_focus, catalog_top;
static unsigned results[VT_IPTV_CHANNEL_MAX],result_count;
static char search[128],saved_search[128];
static int filter_dirty=1;
static int hide_unavailable;
static volatile int probe_cancel;
static volatile long probe_current,probe_total;
static char probe_summary[256];
static int saved_view, starred_view;
static int saved_parent_focus,saved_parent_top;
static char saved_parent_url[VT_IPTV_URL_MAX];
static unsigned artwork_generation,artwork_worker_generation;
static volatile int catalog_cancel;
static VtIptvChannel current_list;
static int current_list_saved, form_link_saved, list_from_form;
#define IPTV_CATALOG_Y 168
/* One geometry for drawing, controller focus and touch hit testing. */
static const int form_rects[6][4]={{18,110,924,90},{18,352,924,48},
    {18,218,596,52},{484,286,458,52},{18,286,458,52},{630,218,312,52}};
static const int form_actions[6]={0,6,1,5,3,4};
static SceUID artwork_thread = -1;
static volatile int artwork_cancel, artwork_done;
static int artwork_index, catalog_visible=6;
static VtOnlineItem artwork_item;
#define TR(id) vt_i18n_str(VT_STR_##id)
static const char *failure_detail(int event);
static void text_line(int x,int y,int width,const char *text,unsigned color,int size);
static void filter_catalog(void) {
    if(!filter_dirty)return;
    result_count=0;
    for(unsigned i=0;i<catalog.count;i++)if(vt_iptv_channel_matches(&catalog.items[i],search) &&
        !(saved_view && starred_view && !catalog.items[i].starred) &&
        !(hide_unavailable && vt_iptv_probe_hidden(catalog.items[i].availability)))results[result_count++]=i;
    if(catalog_focus>=(int)result_count)catalog_focus=result_count?result_count-1:0;
    if(catalog_top>catalog_focus)catalog_top=catalog_focus/2*2;
    filter_dirty=0;
}
static VtIptvChannel *selected_channel(void) {
    filter_catalog();
    return catalog_focus>=0 && catalog_focus<(int)result_count?&catalog.items[results[catalog_focus]]:NULL;
}
static int reload_saved(void) {
    int result=vt_iptv_saved_load(&catalog,starred_view);hide_unavailable=0;filter_dirty=1;return result;
}
static void search_catalog(void) {
    char input[sizeof(search)];
    if(ui_text_input(TR(IPTV_SEARCH),search,input,sizeof(input))<=0)return;
    snprintf(search,sizeof(search),"%s",input);
    if(saved_view)snprintf(saved_search,sizeof(saved_search),"%s",search);
    catalog_focus=catalog_top=0;filter_dirty=1;
}

static void iptv_cover_stop(void) {
    artwork_generation++;
    if(artwork_thread>=0)artwork_cancel=1;
}
void ui_iptv_shutdown(void) {
    iptv_cover_stop();
    if(artwork_thread>=0) {
        sceKernelWaitThreadEnd(artwork_thread,NULL,NULL);
        sceKernelDeleteThread(artwork_thread);artwork_thread=-1;
    }
}
static void restore_saved_selection(const char *url,int fallback,int top) {
    filter_catalog();catalog_focus=fallback;
    for(unsigned i=0;i<result_count;i++)if(url[0]&&!strcmp(catalog.items[results[i]].url,url)){catalog_focus=(int)i;break;}
    if(catalog_focus>=(int)result_count)catalog_focus=result_count?result_count-1:0;
    catalog_top=top;
    if(catalog_top>catalog_focus)catalog_top=catalog_focus/2*2;
}

static int iptv_cover_worker(SceSize args, void *ctx) {
    (void)args; (void)ctx; vt_online_fetch_cover(&artwork_item, &artwork_cancel);
    __sync_synchronize(); artwork_done = 1; return sceKernelExitThread(0);
}
static void iptv_cover_tick(void) {
    if (artwork_thread >= 0 && artwork_done) {
        sceKernelWaitThreadEnd(artwork_thread,NULL,NULL);sceKernelDeleteThread(artwork_thread);artwork_thread=-1;
        if(!artwork_cancel && artwork_worker_generation==artwork_generation && artwork_index<(int)catalog.count)
            snprintf(catalog.items[artwork_index].artwork_path,sizeof(catalog.items[0].artwork_path),
                "%s",artwork_item.artwork_path[0]?artwork_item.artwork_path:"-");
    }
    if (artwork_thread >= 0) return;
    for (int i = catalog_top; i < catalog_top+catalog_visible && i < (int)result_count; i++) {
        VtIptvChannel *item = &catalog.items[results[i]];
        if (!item->logo[0] || item->artwork_path[0]) continue;
        memset(&artwork_item, 0, sizeof(artwork_item));
        snprintf(artwork_item.artwork_url, sizeof(artwork_item.artwork_url), "%s", item->logo);
        artwork_index = results[i]; artwork_cancel = artwork_done = 0;artwork_worker_generation=artwork_generation;
        artwork_thread = sceKernelCreateThread("VmdIptvCover", iptv_cover_worker, 0x10000100, 0x40000, 0, 0, NULL);
        if (artwork_thread >= 0 && sceKernelStartThread(artwork_thread, 0, NULL) < 0) {
            sceKernelDeleteThread(artwork_thread); artwork_thread = -1;
        }
        if (artwork_thread < 0) snprintf(item->artwork_path, sizeof(item->artwork_path), "-");
        break;
    }
}
static int load_catalog(void *ctx) {
    (void)ctx; hide_unavailable=0;unsigned char *data = NULL; size_t size = 0; char effective[VT_IPTV_URL_MAX];
    VtIptvHttpOptions options={0};
    snprintf(options.user_agent,sizeof(options.user_agent),"%s",channel_agent);
    int result = vt_iptv_http_fetch(&options, link, VT_IPTV_PLAYLIST_LIMIT, &data, &size,
        effective, sizeof(effective), &catalog_cancel);
    int fetched=result;
    if (!result) result = vt_iptv_catalog_parse(data, size, effective, &catalog);
    log_printf("IPTV catalog: transport=%d parse=%d bytes=%u entries=%u truncated=%d agent=%d\n",
        fetched,fetched?0:result,(unsigned)size,result?0:catalog.count,catalog.truncated,!!channel_agent[0]);
    free(data);
    VtIptvCatalog *saved = !result ? calloc(1,sizeof(*saved)) : NULL;
    current_list_saved=0;
    if(saved && !vt_iptv_saved_load(saved,0)) {
        for(unsigned j=0;j<saved->count;j++)if(!strcmp(current_list.url,saved->items[j].url)) {
            current_list_saved=1;current_list.starred=saved->items[j].starred;
        }
        for(unsigned i=0;i<catalog.count;i++)for(unsigned j=0;j<saved->count;j++)
            if(!strcmp(catalog.items[i].url,saved->items[j].url))catalog.items[i].starred=saved->items[j].starred;
    }
    if (!result)
        for (unsigned i = 0; i < catalog.count; i++) {
            VtLibraryItem v;
            vt_library_iptv(&catalog.items[i], &v);
            snprintf(v.context, sizeof(v.context), "%s", link);
            vt_library_cache(&v);
        }
    free(saved);return result;
}
static int toggle_star(VtIptvChannel *item) {
    VtIptvCatalog *saved=calloc(1,sizeof(*saved));if(!saved)return -1;
    int result=vt_iptv_saved_load(saved,0);
    if(!result) {
        int old=0;
        for(unsigned i=0;i<saved->count;i++)if(!strcmp(saved->items[i].url,item->url))old=saved->items[i].starred;
        VtIptvChannel updated=*item;
        updated.starred=!old;result=vt_iptv_saved_set(&updated);
        if(!result)*item=updated;
    }
    free(saved);return result;
}
static int player_favorite(void *ctx,int toggle) {
    VtIptvChannel *item=ctx;
    if(toggle) {
        if(toggle_star(item))return -1;
        for(unsigned i=0;i<catalog.count;i++)if(!strcmp(catalog.items[i].url,item->url))catalog.items[i].starred=item->starred;
        filter_dirty=1;
    }
    return item->starred;
}
static int probe_catalog(void *ctx) {
    (void)ctx;
    /* One request at a time: no decoder, segment fetch, extra worker pool or
     * downloaded media. Results are temporary observations, not deletions. */
    for(unsigned i=0;i<catalog.count;i++) {
        if(__atomic_load_n(&probe_cancel,__ATOMIC_ACQUIRE) || !vita_https_is_connected())break;
        VtIptvChannel *item=&catalog.items[i];if(item->mode==VT_IPTV_LIST)continue;
        VtIptvHttpOptions options={0};snprintf(options.user_agent,sizeof(options.user_agent),"%s",item->user_agent);
        item->availability=vt_iptv_http_probe(&options,item->url,&probe_cancel);
        if(__atomic_load_n(&probe_cancel,__ATOMIC_ACQUIRE))break;
        __atomic_add_fetch(&probe_current,1,__ATOMIC_RELEASE);
    }
    return 0;
}
static void availability_filter(const char **message) {
    if(hide_unavailable){hide_unavailable=0;filter_dirty=1;*message="";return;}
    iptv_cover_stop();probe_current=probe_total=0;probe_cancel=0;
    for(unsigned i=0;i<catalog.count;i++) {
        catalog.items[i].availability=VT_IPTV_PROBE_UNKNOWN;
        if(catalog.items[i].mode!=VT_IPTV_LIST)probe_total++;
    }
    if(probe_total)ui_loading_run(TR(IPTV_CHECKING),probe_catalog,NULL,&probe_cancel,&probe_current,&probe_total);
    unsigned failed=0;for(unsigned i=0;i<catalog.count;i++)failed+=vt_iptv_probe_hidden(catalog.items[i].availability);
    snprintf(probe_summary,sizeof(probe_summary),TR(IPTV_CHECK_RESULT),(unsigned)probe_current,failed);
    log_printf("IPTV availability: checked=%ld total=%ld hidden=%u cancelled=%d\n",probe_current,probe_total,failed,probe_cancel);
    hide_unavailable=1;filter_dirty=1;catalog_focus=catalog_top=0;*message=probe_summary;
}
static int save_link(VtIptvChannel *item) {
    VtIptvCatalog *saved=calloc(1,sizeof(*saved));if(!saved)return -1;
    int result=vt_iptv_saved_load(saved,0);
    if(!result) {
        for(unsigned i=0;i<saved->count;i++)if(!strcmp(saved->items[i].url,item->url)) {
            item->starred=saved->items[i].starred;
            if(!item->logo[0])snprintf(item->logo,sizeof(item->logo),"%s",saved->items[i].logo);
            if(!item->user_agent[0])snprintf(item->user_agent,sizeof(item->user_agent),"%s",saved->items[i].user_agent);
            if(!item->group[0])snprintf(item->group,sizeof(item->group),"%s",saved->items[i].group);
            if(!item->channel_id[0])snprintf(item->channel_id,sizeof(item->channel_id),"%s",saved->items[i].channel_id);
            break;
        }
        result=vt_iptv_saved_set(item);
    }
    free(saved);return result;
}
/* Work on a copy: cancelled/invalid input or a failed write must not rename
 * the visible entry, lose its starred state, or create a second saved URL. */
static int rename_saved_link(VtIptvChannel *item, const char **message) {
    if (!item || !item->url[0]) return 0;
    VtIptvChannel renamed = *item;
    char input[sizeof(item->title)];
    if (ui_text_input(TR(IPTV_NAME),item->title,input,sizeof(input)) <= 0) return 0;
    input[sizeof(input)-1]=0;
    char *start=input; while (*start==' ' || *start=='\t') start++;
    size_t length=strlen(start);
    while(length && (start[length-1]==' ' || start[length-1]=='\t'))length--;
    int valid=length>0;
    for(size_t i=0;i<length;i++)if((unsigned char)start[i]<32 || start[i]==127)valid=0;
    if(!valid){*message=TR(IPTV_NAME_INVALID);return -1;}
    memcpy(renamed.title,start,length);renamed.title[length]=0;
    if(save_link(&renamed)){*message=TR(IPTV_SAVE_FAILED);return -1;}
    *item=renamed;
    filter_dirty=1;
    if(!strcmp(current_list.url,item->url)){current_list=renamed;current_list_saved=1;}
    if(!strcmp(link,item->url)){snprintf(channel_title,sizeof(channel_title),"%s",renamed.title);form_link_saved=1;}
    *message=TR(IPTV_SAVED);return 1;
}
static void open_list(const char **message) {
    if(saved_view)snprintf(saved_search,sizeof(saved_search),"%s",search);
    char checked[VT_IPTV_URL_MAX];
    int valid=vt_iptv_input_url(link,strlen(link),checked,sizeof(checked));
    if(valid){*message=valid==VT_IPTV_LIMIT?TR(IPTV_TOO_LARGE):TR(IPTV_INVALID);return;}
    snprintf(link,sizeof(link),"%s",checked);
    search[0]=0;filter_dirty=1;
    iptv_cover_stop();catalog_cancel=0;saved_view=0;
    int result=ui_loading_run(TR(IPTV_MODE_LIST),load_catalog,NULL,&catalog_cancel,NULL,NULL);
    if(result<0){
        catalog.count=0;
        *message=result==VT_IPTV_CANCELLED?"":result==VT_IPTV_INVALID || result==VT_IPTV_UNSUPPORTED
            ?TR(IPTV_LIST_INVALID):failure_detail(result);
        if(result!=VT_IPTV_CANCELLED)ui_message_show(TR(IPTV_FAILED),*message,12000);
    } else *message=catalog.truncated?TR(IPTV_LIST_LIMIT):"";
    log_save(VITAMEDIADECK_SESSION_LOG_PATH);
    catalog_focus=catalog_top=0;
}

/* A fresh input edge and Cancel-first focus prevent the menu's held X from
 * removing an entry. Only the saved reference is deleted, not its contents. */
static int confirm_remove(const VtIptvChannel *item) {
    int selected=0;UiFocusMotion motion={0};SceCtrlData previous={0};
    sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();
    for(;;) {
        ui_mini_player_pump();SceCtrlData controls={0};UiTouchEvent touch={0};
        sceCtrlPeekBufferPositive(0,&controls,1);
        unsigned pressed=controls.buttons&~previous.buttons;previous=controls;
        ui_mini_player_handle_buttons(&pressed);
        unsigned touches=ui_touch_poll(&touch);
        if(ui_mini_player_input_locked())pressed=touches=0;
        if(pressed&(SCE_CTRL_CIRCLE|SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER))return 0;
        if(pressed&SCE_CTRL_LEFT)selected=0;
        if(pressed&SCE_CTRL_RIGHT)selected=1;
        if(touches&UI_TOUCH_EVENT_TAP)for(int i=0;i<2;i++)
            if(ui_touch_hit_rect(touch.x,touch.y,200+i*285,350,275,50)) {
                selected=i;pressed|=SCE_CTRL_CROSS;
            }
        if(pressed&SCE_CTRL_CROSS)return selected;
        vita2d_start_drawing();vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG,VT_THEME_SIGNAL_BRIGHT);
        ui_brand_draw_breadcrumbs(TR(IPTV_TITLE),NULL,TR(IPTV_DELETE));
        ui_modal_panel(174,96,612,326);
        text_line(200,140,560,TR(IPTV_DELETE_TITLE),VT_THEME_TEXT,UI_FONT_BODY);
        text_line(200,182,560,item->title,VT_THEME_TEXT,UI_FONT_BODY);
        ui_text_block(200,236,560,28,3,TR(IPTV_DELETE_DETAIL),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        ui_action_button(200,350,275,50,VT_THEME_SURFACE_RAISED,"X",TR(NETWORK_CANCEL),0);
        ui_action_button(485,350,275,50,VT_THEME_SURFACE_RAISED,"X",TR(IPTV_DELETE),0);
        ui_focus_motion_tick(&motion,200+selected*285,350,275,50);ui_focus_outline_draw(&motion);
        vita2d_end_drawing();vita2d_wait_rendering_done();vita2d_swap_buffers();sceKernelDelayThread(1000);
    }
}
static int remove_saved_link(const VtIptvChannel *item,const char **message) {
    if(!item || !item->url[0] || !confirm_remove(item))return 0;
    int result=vt_iptv_saved_remove(item->url);
    log_printf("IPTV saved remove: result=%d mode=%d\n",result,item->mode);
    log_save(VITAMEDIADECK_SESSION_LOG_PATH);
    if(result){*message=TR(IPTV_DELETE_FAILED);return -1;}
    if(!strcmp(current_list.url,item->url))memset(&current_list,0,sizeof(current_list));
    *message=TR(IPTV_DELETED);return 1;
}
static void new_link(void) {
    search[0]=0;filter_dirty=1;
    link[0]=channel_title[0]=channel_agent[0]=0;
    memset(&current_list,0,sizeof(current_list));
    catalog.count=0;saved_view=starred_view=0;mode=VT_IPTV_HLS;
    current_list_saved=form_link_saved=list_from_form=0;
}
static int accept_link(const char *input) {
    if(!input)return -1;
    char checked[VT_IPTV_URL_MAX];
    int result=vt_iptv_input_url(input,strlen(input),checked,sizeof(checked));
    if(result) {
        log_printf("IPTV input rejected: result=%d bytes=%u\n",result,(unsigned)strlen(input));
        log_save(VITAMEDIADECK_SESSION_LOG_PATH);
        return result;
    }
    snprintf(link,sizeof(link),"%s",checked);
    channel_title[0]=channel_agent[0]=0;mode=vt_iptv_link_kind(link);
    form_link_saved=current_list_saved=0;
    memset(&current_list,0,sizeof(current_list));
    log_printf("IPTV link input: mode=%d length=%u\n",mode,(unsigned)strlen(link));
    log_save(VITAMEDIADECK_SESSION_LOG_PATH);
    return 0;
}

static const char *failure_detail(int event) {
    switch (event) {
    case VT_IPTV_DISCONTINUITY: case VT_IPTV_LAGGED: return TR(IPTV_CHANGED);
    case VT_IPTV_LIMIT: case VT_IPTV_MEMORY: return TR(IPTV_TOO_LARGE);
    case VT_IPTV_INVALID: return TR(IPTV_MANIFEST_ERROR);
    case UI_LOADING_TASK_FAILED: return TR(IPTV_BACKGROUND_ERROR);
    case VT_IPTV_MANIFEST: return TR(IPTV_MANIFEST_ERROR);
    case VT_IPTV_VIDEO_CODEC: return TR(IPTV_VIDEO_CODEC_ERROR);
    case VT_IPTV_AUDIO_CODEC: return TR(IPTV_AUDIO_CODEC_ERROR);
    case VT_IPTV_VIDEO_SIZE: return TR(IPTV_VIDEO_SIZE_ERROR);
    case VT_IPTV_AUDIO_FORMAT: return TR(IPTV_AUDIO_FORMAT_ERROR);
    case VT_IPTV_HTTP_SERVER: return TR(IPTV_SERVER_ERROR);
    case VT_IPTV_HTTP_RATE_LIMIT: return TR(IPTV_RATE_LIMIT_ERROR);
    case VT_IPTV_RANGE: return TR(IPTV_RANGE_ERROR);
    case VT_IPTV_UNSUPPORTED: return TR(IPTV_UNSUPPORTED);
    case VT_IPTV_IO: return TR(IPTV_NETWORK_ERROR);
    case VT_IPTV_DNS: return TR(IPTV_DNS_ERROR);
    case VT_IPTV_ACCESS: return TR(IPTV_ACCESS_ERROR);
    case VT_IPTV_NOT_FOUND: return TR(IPTV_NOT_FOUND_ERROR);
    case VT_IPTV_TIMEOUT: return TR(IPTV_TIMEOUT_ERROR);
    case VT_IPTV_TLS: return TR(IPTV_TLS_ERROR);
    case VT_IPTV_REDIRECT: return TR(IPTV_REDIRECT_ERROR);
    case VT_IPTV_ENCRYPTED: return TR(IPTV_ENCRYPTED_ERROR);
    case VT_IPTV_RENDITIONS: return TR(IPTV_RENDITIONS_ERROR);
    case VT_IPTV_VARIANT_LIMIT: return TR(IPTV_VARIANT_LIMIT_ERROR);
    default: return TR(IPTV_DECODE_ERROR);
    }
}
static void display_link(char out[VT_IPTV_URL_MAX]) {
    snprintf(out, VT_IPTV_URL_MAX, "%s", link);
    char *query = strchr(out, '?');
    if (query) *query = 0; /* Never expose an access token in the page/HUD. */
}
static int play_link(const char **message) {
    char checked[VT_IPTV_URL_MAX];
    if (vt_iptv_resolve_url(link, link, checked, sizeof(checked))) {
        *message = TR(IPTV_INVALID); return UI_SECTION_NONE;
    }
    VtIptvChannel playing={0};
    snprintf(playing.url,sizeof(playing.url),"%s",link);
    snprintf(playing.title,sizeof(playing.title),"%s",channel_title[0]?channel_title:TR(IPTV_TITLE));
    snprintf(playing.user_agent,sizeof(playing.user_agent),"%s",channel_agent);playing.mode=mode;
    for(unsigned i=0;i<catalog.count;i++)if(!strcmp(catalog.items[i].url,link)){playing=catalog.items[i];break;}
    VtIptvCatalog *saved_items=calloc(1,sizeof(*saved_items));
    if(saved_items && !vt_iptv_saved_load(saved_items,0))
        for(unsigned i=0;i<saved_items->count;i++)if(!strcmp(saved_items->items[i].url,link)) {
            playing.starred=saved_items->items[i].starred;
            if(!playing.logo[0])snprintf(playing.logo,sizeof(playing.logo),"%s",saved_items->items[i].logo);
            break;
        }
    free(saved_items);
    VtLibraryItem library_item;
    vt_library_iptv(&playing, &library_item);
    vt_library_cache(&library_item);
    vt_video_thumbnail_prepare_playback();
    vt_background_playback_set_fullscreen_resume(NULL, NULL);
    /* Release the previous audio port and fence mini-player video before
     * opening a second decoder. request_stop alone is asynchronous. */
    vt_background_playback_stop();
    if (mode == VT_IPTV_FILE) {
        VtHwPlayerScreenSource source = {0};
        source.favorite=player_favorite;source.favorite_ctx=&playing;
        source.history_id = library_item.history_id;
        source.start_position_ms = vt_library_resume(&library_item);
        vt_http_audio_stream_factory(link, &source.stream);
        source.title = channel_title[0] ? channel_title : TR(IPTV_TITLE);
        source.location = TR(IPTV_MODE_FILE); source.authenticated_remote = 1;
        int result = vt_hw_player_screen_run(&source, NULL, NULL, NULL, NULL, NULL);
        *message = result < 0 ? TR(IPTV_DECODE_ERROR) : TR(IPTV_EXIT_NOTE);
        if (result < 0) ui_message_show(TR(IPTV_FAILED),*message,12000);
        if (result < 0) { log_printf("IPTV VOD file: result=%d\n", result); log_save(VITAMEDIADECK_SESSION_LOG_PATH); }
        return result >= VT_HW_PLAYER_ACTION_SECTION_BASE && result < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT
            ? result - VT_HW_PLAYER_ACTION_SECTION_BASE : UI_SECTION_NONE;
    }
    for (;;) {
        VtIptvSourceConfig config = {0};
        config.buffer_limit = 24u * 1024u * 1024u;
        config.width_limit = 1280;
        config.height_limit = 720;
        /* Both segmented protocols provide synchronized one-second TS chunks. */
        config.startup_segments = 6;
        VtIptvHttpOptions http_options={0};
        snprintf(http_options.user_agent,sizeof(http_options.user_agent),"%s",channel_agent);
        config.fetch_ctx=&http_options;
        config.fetch_range=vt_iptv_http_fetch_range;
        if(mode==VT_IPTV_DASH)vt_dash_source_configure(&config);
        else vt_hls_source_configure(&config);
        VtIptvSession *session = vt_iptv_session_create(link, &config);
        if (!session) { *message = TR(IPTV_TOO_LARGE); return UI_SECTION_NONE; }
        VtHwPlayerScreenSource source = {0};
        source.favorite=player_favorite;source.favorite_ctx=&playing;
        vt_iptv_session_factory(session, &source.stream);
        source.title = channel_title[0] ? channel_title : TR(IPTV_TITLE);
        source.location = current_list.title[0] ? current_list.title : TR(IPTV_TITLE);
        source.authenticated_remote = 1;
        source.history_id = library_item.history_id;
        int result = vt_hw_player_screen_run_live(&source, session);
        int event = vt_iptv_session_event(session);
        VtIptvSourceStatus status;
        vt_iptv_session_status(session, &status);
        /* Also cover normal exits and loader-allocation failures: no UI-thread
         * network join. The producer owns its HTTP state until it acknowledges
         * cancel; redraw rather than freeing in-flight callback contexts. */
        vt_iptv_session_cancel(session);
        UiPlayerLoadingInfo closing={.title=source.title,.status=TR(IPTV_CLOSING)};
        while(!vt_iptv_session_stopped(session)) {
            ui_player_loading_present(&closing,NULL,NULL);sceKernelDelayThread(16000);
        }
        vt_iptv_session_destroy(session);
        log_printf("IPTV playback: result=%d source=%d generation=%llu segments=%u bytes=%u\n",
            result, event, (unsigned long long)status.generation,
            status.buffered_segments, (unsigned)status.buffered_bytes);
        if (result < 0) {
            *message = failure_detail(event && event!=VT_IPTV_CANCELLED ? event : status.last_error && status.last_error!=VT_IPTV_CANCELLED ? status.last_error : !status.buffered_segments ? VT_IPTV_IO : 0);
            log_save(VITAMEDIADECK_SESSION_LOG_PATH);
            /* A short startup rejection must not look like an unexplained
             * player exit. Keep the reason in the footer after the notice. */
            ui_message_show(TR(IPTV_FAILED), *message, 12000);
        } else if(event==VT_IPTV_CANCELLED) *message=TR(IPTV_CANCELLED);
        else if (status.terminal == VT_IPTV_END) *message = TR(IPTV_FINISHED);
        else *message = TR(IPTV_EXIT_NOTE);
        if (result == VT_HW_PLAYER_ACTION_GO_LIVE) continue;
        if (result >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
            result < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT)
            return result - VT_HW_PLAYER_ACTION_SECTION_BASE;
        return UI_SECTION_NONE;
    }
}
static void text_line(int x, int y, int width, const char *text, unsigned color, int size) {
    vita2d_font *font = ui_runtime_font(size);
    if (!font) return;
    char fitted[512];
    ui_font_fit_text(font, size, text, fitted, sizeof(fitted), width);
    ui_font_draw_text(font, x, y, color, size, fitted);
}
static const char *mode_label(int kind) {
    return vt_i18n_str(kind==VT_IPTV_DASH?VT_STR_IPTV_MODE_DASH:kind==VT_IPTV_FILE?VT_STR_IPTV_MODE_FILE:
        kind==VT_IPTV_LIST?VT_STR_IPTV_MODE_LIST:VT_STR_IPTV_MODE_HLS);
}
static int options_for_page(UiContextAction items[8]) {
    int n=0,listing=catalog.count || saved_view;
    if(listing) {
        VtIptvChannel *item=selected_channel();
        if(item) {
            items[n++]=(UiContextAction){item->mode==VT_IPTV_LIST?TR(IPTV_MODE_LIST):TR(IPTV_PLAY),NULL,18,1};
            items[n++]=(UiContextAction){item->starred?TR(UI_UNSTAR):TR(UI_STAR),NULL,10,1};
            if(saved_view) {
                items[n++]=(UiContextAction){TR(IPTV_RENAME),NULL,14,1};
                items[n++]=(UiContextAction){TR(IPTV_DELETE),NULL,15,1};
            }
        }
        if(saved_view)items[n++]=(UiContextAction){starred_view?TR(UI_SHOW_ALL):TR(UI_SHOW_STARRED),NULL,11,1};
        else {
            items[n++]=(UiContextAction){current_list.starred?TR(IPTV_UNSTAR_LIST):TR(IPTV_STAR_LIST),NULL,12,current_list.url[0]!=0};
            items[n++]=(UiContextAction){TR(IPTV_SAVE_LIST),NULL,16,current_list.url[0]!=0};
        }
        items[n++]=(UiContextAction){TR(IPTV_SEARCH),NULL,19,1};
        items[n++]=(UiContextAction){TR(UI_REFRESH),NULL,13,1};
        if(saved_view)items[n++]=(UiContextAction){TR(IPTV_ADD),NULL,0,1};
        else {
            items[n++]=(UiContextAction){hide_unavailable?TR(IPTV_SHOW_UNAVAILABLE):TR(IPTV_CHECK_FILTER),NULL,17,catalog.count>0};
            items[n++]=(UiContextAction){TR(UI_BACK),NULL,5,1};
        }
    } else {
        items[n++]=(UiContextAction){TR(IPTV_EDIT),NULL,0,1};
        items[n++]=(UiContextAction){TR(IPTV_QR),NULL,3,1};
        items[n++]=(UiContextAction){TR(IPTV_SAVE),NULL,4,link[0]!=0};
        items[n++]=(UiContextAction){TR(IPTV_COLLECTION),NULL,5,1};
    }
    return n;
}
static void catalog_actions(UiContextAction items[4]) {
    VtIptvChannel *item=selected_channel();
    items[0]=(UiContextAction){item && item->mode==VT_IPTV_LIST?TR(IPTV_OPEN_SHORT):TR(IPTV_PLAY_SHORT),"X",18,item!=NULL};
    items[1]=(UiContextAction){item && item->starred?TR(IPTV_UNSTAR_SHORT):TR(IPTV_STAR_SHORT),"Square",10,item!=NULL};
    items[2]=(UiContextAction){saved_view?TR(IPTV_RENAME):current_list_saved?TR(IPTV_RENAME_LIST):TR(IPTV_SAVE_LIST_SHORT),"Select",saved_view?14:16,saved_view?item!=NULL:current_list.url[0]!=0};
    items[3]=(UiContextAction){saved_view?TR(IPTV_DELETE_SHORT):current_list.starred?TR(IPTV_UNSTAR_LIST_SHORT):TR(IPTV_STAR_LIST_SHORT),NULL,saved_view?15:12,saved_view?item!=NULL:current_list.url[0]!=0};
}
int ui_iptv_screen(void) {
    const char *message = "";
    int focus = 0, action_focus=-1;
    UiContextMenu options={0};
    UiFocusMotion focus_motion={0};
    if(!catalog.count) { saved_view=1; starred_view=0; catalog_focus=catalog_top=0;
        if(reload_saved())message=TR(IPTV_SAVE_FAILED); }
    filter_dirty=1;filter_catalog();
    UiSectionsSidebar sidebar;
    ui_sections_sidebar_init(&sidebar, UI_SECTION_IPTV);
    UiNavRepeat repeat;
    ui_nav_repeat_reset(&repeat);
    SceCtrlData controls = {0}, previous = {0};
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceCtrlPeekBufferPositive(0, &previous, 1);
    ui_touch_reset();
    for (;;) {
        ui_mini_player_pump();
        sceCtrlPeekBufferPositive(0, &controls, 1);
        unsigned pressed = controls.buttons & ~previous.buttons;
        previous = controls;
        ui_mini_player_handle_buttons(&pressed);
        UiTouchEvent touch;
        unsigned touches = ui_touch_poll(&touch);
        if (ui_mini_player_input_locked()) {
            pressed = touches = 0; controls.buttons &= SCE_CTRL_SELECT;
            controls.lx = controls.ly = 128;
        }
        if (ui_brand_sections_hit(touches, touch.x, touch.y)) {
            pressed |= SCE_CTRL_LTRIGGER; touches = 0;
        }
        if((touches&UI_TOUCH_EVENT_TAP) && ui_touch_hit_rect(touch.x,touch.y,756,UI_TABS_Y,186,40)) {
            pressed|=SCE_CTRL_RTRIGGER; touches=0;
        }
        if(pressed&SCE_CTRL_RTRIGGER)sidebar.open=0;
        UiContextAction option_items[8];int option_count=options_for_page(option_items);
        int option_action=ui_context_menu_input(&options,option_items,option_count,&pressed,controls.buttons,controls.ly,&touches,&touch);
        if(!options.open && options.animation<=.01f && !sidebar.open && sidebar.animation<=.01f &&
            (catalog.count || saved_view) && (touches&UI_TOUCH_EVENT_TAP) &&
            ui_touch_hit_rect(touch.x,touch.y,564,UI_TABS_Y,176,40)) { option_action=saved_view?11:17;touches=0; }
        ui_context_menu_tick(&options);
        int was_open = sidebar.open;
        int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
                                                         controls.buttons, controls.ly);
        if (was_open || sidebar.open) {
            int selected = ui_sections_sidebar_handle_touch(&sidebar, touches, touch.x, touch.y);
            if (selected != UI_SECTION_NONE) section = selected;
            touches = 0;
        }
        if (section != UI_SECTION_NONE) { iptv_cover_stop(); return section; }
        ui_sections_sidebar_tick(&sidebar);
        int page_input = !was_open && !sidebar.open && sidebar.animation <= 0.01f && !options.open && options.animation<=.01f;
        if(option_action>=0) page_input=1;
        if(page_input && (catalog.count || saved_view)) {
            UiContextAction bar[4];catalog_actions(bar);
            unsigned nav=ui_nav_repeat_update(&repeat,pressed,controls.buttons,controls.lx,controls.ly,
                SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_UP|SCE_CTRL_DOWN);
            if(action_focus>=0) {
                if(nav&SCE_CTRL_LEFT)action_focus=action_focus>0?action_focus-1:0;
                else if(nav&SCE_CTRL_RIGHT)action_focus=action_focus<3?action_focus+1:3;
                else if(nav&SCE_CTRL_DOWN){action_focus=-1;catalog_focus=catalog_top=0;}
                else if((pressed&SCE_CTRL_CROSS)&&bar[action_focus].enabled)option_action=bar[action_focus].action;
                pressed&=~(SCE_CTRL_CROSS|SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
                controls.buttons&=~(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
                controls.lx=controls.ly=128;
            } else if((nav&SCE_CTRL_UP)&&catalog_focus<2) {
                action_focus=catalog_focus%2;pressed&=~SCE_CTRL_UP;
                controls.buttons&=~SCE_CTRL_UP;controls.lx=controls.ly=128;
            } else {
                /* Navigation is consumed once; repeating it below would skip items. */
                if((nav&SCE_CTRL_LEFT)&&catalog_focus%2)catalog_focus--;
                else if((nav&SCE_CTRL_RIGHT)&&!(catalog_focus%2)&&catalog_focus+1<(int)result_count)catalog_focus++;
                else if((nav&SCE_CTRL_UP)&&catalog_focus>=2)catalog_focus-=2;
                else if((nav&SCE_CTRL_DOWN)&&catalog_focus/2*2+2<(int)result_count) {
                    catalog_focus+=2;if(catalog_focus>=(int)result_count)catalog_focus=result_count-1;
                }
            }
            if(pressed&SCE_CTRL_SELECT){option_action=saved_view?14:16;pressed&=~SCE_CTRL_SELECT;}
            if(touches&UI_TOUCH_EVENT_TAP)for(int i=0;i<4;i++)
                if(ui_touch_hit_rect(touch.x,touch.y,18+i*233,110,225,46)) {
                    action_focus=i;if(bar[i].enabled)option_action=bar[i].action;touches=0;
                }
        }
        int root_action=-1;
        if(option_action>=0 && option_action<10) {
            iptv_cover_stop();catalog.count=0;saved_view=0;action_focus=-1;root_action=option_action;
        }
        if(option_action==10)pressed|=SCE_CTRL_SQUARE;
        if(option_action==18)pressed|=SCE_CTRL_CROSS;
        if(option_action==19)pressed|=SCE_CTRL_TRIANGLE;
        if(option_action==11 && saved_view){iptv_cover_stop();starred_view=!starred_view;reload_saved();catalog_focus=catalog_top=0;}
        if(option_action==12 && !saved_view && current_list.url[0]) {
            int old=current_list.starred;
            int result=toggle_star(&current_list);
            message=result?TR(IPTV_SAVE_FAILED):old?TR(ONLINE_REMOVED):TR(IPTV_SAVED);
            if(!result){current_list_saved=1;if(!strcmp(link,current_list.url))form_link_saved=1;}
        }
        if(option_action==17) {
            availability_filter(&message);filter_catalog();memset(&focus_motion,0,sizeof(focus_motion));
            sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
            pressed=touches=0;page_input=0;
        }
        if(option_action==13) {
            iptv_cover_stop();
            if(saved_view) { reload_saved(); catalog_focus=catalog_top=0; }
            else {
                char previous_search[sizeof(search)];snprintf(previous_search,sizeof(previous_search),"%s",search);
                snprintf(link,sizeof(link),"%s",current_list.url);
                snprintf(channel_agent,sizeof(channel_agent),"%s",current_list.user_agent);
                open_list(&message);
                snprintf(search,sizeof(search),"%s",previous_search);filter_dirty=1;
            }
        }
        if(option_action==14 && saved_view && selected_channel()) {
            iptv_cover_stop();
            rename_saved_link(selected_channel(),&message);
            sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
            pressed=touches=0;page_input=0;
        }
        if(option_action==15 && saved_view && selected_channel()) {
            iptv_cover_stop();
            if(remove_saved_link(selected_channel(),&message)>0) {
                if(reload_saved())message=TR(IPTV_SAVE_FAILED);
                if(catalog_focus>=(int)catalog.count)catalog_focus=catalog.count?catalog.count-1:0;
                if(catalog_top>catalog_focus)catalog_top=catalog_focus/2*2;
                memset(&focus_motion,0,sizeof(focus_motion));
            }
            sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
            pressed=touches=0;page_input=0;
        }
        if(option_action==16 && !saved_view && current_list.url[0]) {
            iptv_cover_stop();rename_saved_link(&current_list,&message);
            sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
            pressed=touches=0;page_input=0;
        }
        int catalog_rows=(ui_mini_player_top()-IPTV_CATALOG_Y-28+8)/112;
        if(catalog_rows<1)catalog_rows=1;
        if(catalog_rows>4)catalog_rows=4;
        int catalog_step=(ui_mini_player_top()-IPTV_CATALOG_Y-28+8)/catalog_rows;
        catalog_visible=catalog_rows*2;
        if (page_input && ui_mini_player_handle_touch(touches, &touch)) touches = 0;
        filter_catalog();
        if (catalog.count || saved_view) {
            if (catalog_focus < catalog_top) catalog_top = catalog_focus/2*2;
            if (catalog_focus >= catalog_top+catalog_visible) catalog_top = (catalog_focus/2-catalog_rows+1)*2;
            iptv_cover_tick(); vt_video_thumbnail_resume(); vt_video_thumbnail_pump();
        }
        if (page_input && (catalog.count || saved_view)) {
            if((touches&UI_TOUCH_EVENT_TAP) && ui_touch_hit_rect(touch.x,touch.y,272,UI_TABS_Y,276,40)) {
                pressed|=SCE_CTRL_TRIANGLE;touches=0;
            }
            if(pressed&SCE_CTRL_TRIANGLE) {
                iptv_cover_stop();search_catalog();filter_catalog();message="";
                memset(&focus_motion,0,sizeof(focus_motion));
                sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
                pressed=touches=0;
            }
            if ((touches & UI_TOUCH_EVENT_UP) && !(touches & UI_TOUCH_EVENT_TAP) && touch.down_y>=IPTV_CATALOG_Y && touch.down_y<ui_mini_player_top()-28) {
                catalog_focus+=(touch.down_y-touch.y)/catalog_step*2;
                if(catalog_focus<0)catalog_focus=0;
                if(catalog_focus>=(int)result_count)catalog_focus=result_count?result_count-1:0;
            }
            if (touches & UI_TOUCH_EVENT_TAP) for (int i=0;i<catalog_visible && catalog_top+i<(int)result_count;i++)
                if(ui_touch_hit_rect(touch.x,touch.y,UI_CONTENT_X+i%2*466,IPTV_CATALOG_Y+i/2*catalog_step,458,catalog_step-8)) {
                    catalog_focus=catalog_top+i;action_focus=-1;pressed|=SCE_CTRL_CROSS;
                }
            if(pressed&SCE_CTRL_SQUARE && selected_channel()) {
                VtIptvChannel *item=selected_channel();char keep[VT_IPTV_URL_MAX];snprintf(keep,sizeof(keep),"%s",item->url);
                int was_starred=item->starred,old_focus=catalog_focus,old_top=catalog_top;
                iptv_cover_stop();message=toggle_star(item)?TR(IPTV_SAVE_FAILED):was_starred?TR(ONLINE_REMOVED):TR(IPTV_SAVED);
                if(saved_view){reload_saved();restore_saved_selection(keep,old_focus,old_top);}
            }
            if ((touches&UI_TOUCH_EVENT_TAP) && ui_touch_hit_rect(touch.x,touch.y,UI_CONTENT_X,UI_TABS_Y,240,40))pressed|=SCE_CTRL_START;
            if(pressed&SCE_CTRL_START) { iptv_cover_stop();new_link();focus=0;action_focus=-1;message="";memset(&focus_motion,0,sizeof(focus_motion)); }
            else if (pressed & SCE_CTRL_CIRCLE) {
                iptv_cover_stop();
                if(saved_view)return UI_SECTION_LOCAL_MEDIA;
                action_focus=-1;
                if(list_from_form) {
                    snprintf(link,sizeof(link),"%s",current_list.url);
                    snprintf(channel_title,sizeof(channel_title),"%s",current_list.title);
                    snprintf(channel_agent,sizeof(channel_agent),"%s",current_list.user_agent);
                    mode=VT_IPTV_LIST;form_link_saved=current_list_saved;
                    catalog.count=0;saved_view=0;search[0]=0;filter_dirty=1;focus=2;
                } else {
                    saved_view=1;snprintf(search,sizeof(search),"%s",saved_search);
                    reload_saved();restore_saved_selection(saved_parent_url,saved_parent_focus,saved_parent_top);
                }
            }
            else if ((pressed & SCE_CTRL_CROSS) && selected_channel()) {
                iptv_cover_stop(); VtIptvChannel *item=selected_channel();
                snprintf(link,sizeof(link),"%s",item->url); snprintf(channel_title,sizeof(channel_title),"%s",item->title);
                snprintf(channel_agent,sizeof(channel_agent),"%s",item->user_agent);
                mode=item->mode;
                if(mode==VT_IPTV_LIST){
                    if(saved_view){saved_parent_focus=catalog_focus;saved_parent_top=catalog_top;snprintf(saved_parent_url,sizeof(saved_parent_url),"%s",item->url);}
                    list_from_form=0;current_list=*item;open_list(&message);action_focus=-1;
                }
                else {
                    section=play_link(&message);
                    if(section!=UI_SECTION_NONE && section!=UI_SECTION_IPTV)return section;
                }
                sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
            }
        } else if (page_input) {
            unsigned nav = ui_nav_repeat_update(&repeat, pressed, controls.buttons,
                controls.lx, controls.ly, SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_UP | SCE_CTRL_DOWN);
            static const int up[6]={0,4,0,5,2,0},down[6]={2,1,4,1,1,3};
            static const int left[6]={0,1,2,4,4,2},right[6]={0,1,5,3,3,5};
            if(nav&SCE_CTRL_UP)focus=up[focus];
            else if(nav&SCE_CTRL_DOWN)focus=down[focus];
            else if(nav&SCE_CTRL_LEFT){if(focus==1)mode=(mode+3)%4;else focus=left[focus];}
            else if(nav&SCE_CTRL_RIGHT){if(focus==1)mode=(mode+1)%4;else focus=right[focus];}
            int action=root_action>=0?root_action:(pressed&SCE_CTRL_CROSS)?form_actions[focus]:-1;
            if(pressed&SCE_CTRL_START)action=0;
            else if(pressed&SCE_CTRL_TRIANGLE)action=3;
            else if(pressed&SCE_CTRL_SQUARE)action=4;
            else if(pressed&SCE_CTRL_SELECT)action=6;
            else if(pressed&SCE_CTRL_CIRCLE)action=5;
            if(touches&UI_TOUCH_EVENT_TAP)for(int i=0;i<6;i++)
                if(ui_touch_hit_rect(touch.x,touch.y,form_rects[i][0],form_rects[i][1],form_rects[i][2],form_rects[i][3])) {focus=i;action=form_actions[i];}
            if (action == 0) {
                char input[VT_IPTV_URL_MAX];
                if (ui_text_input(TR(IPTV_LINK), link, input, sizeof(input)) > 0) {
                    if (!accept_link(input)) {
                        focus = 2; message = "";
                    } else message = TR(IPTV_INVALID);
                }
            } else if (action == 1 && !link[0]) {
                focus=0;message=TR(IPTV_EMPTY);
            } else if (action == 1) {
                if(mode==VT_IPTV_LIST) {
                    memset(&current_list,0,sizeof(current_list));snprintf(current_list.url,sizeof(current_list.url),"%s",link);
                    snprintf(current_list.title,sizeof(current_list.title),"%s",channel_title[0]?channel_title:TR(IPTV_MODE_LIST));current_list.mode=VT_IPTV_LIST;
                    list_from_form=1;open_list(&message);action_focus=-1;section=UI_SECTION_NONE;
                } else section = play_link(&message);
                if (section != UI_SECTION_NONE && section != UI_SECTION_IPTV) return section;
            } else if(action==3) {
                char scanned[VT_IPTV_URL_MAX];
                int scan=ui_qr_scan_https_url(scanned,sizeof(scanned));
                if(scan>0){if(!accept_link(scanned)){focus=2;message="";}else message=TR(IPTV_INVALID);}
                else if(scan<0)message=ui_qr_scan_last_error();
            } else if(action==4 && !link[0]) {
                focus=0;message=TR(IPTV_EMPTY);
            } else if(action==4) {
                VtIptvChannel item={0};char checked[VT_IPTV_URL_MAX];
                if(vt_iptv_resolve_url(link,link,checked,sizeof(checked)))message=TR(IPTV_INVALID);
                else {
                    snprintf(item.url,sizeof(item.url),"%s",link);item.mode=mode;
                    snprintf(item.user_agent,sizeof(item.user_agent),"%s",channel_agent);
                    snprintf(item.title,sizeof(item.title),"%s",channel_title);
                    rename_saved_link(&item,&message);
                }
            } else if(action==5) {
                iptv_cover_stop();saved_view=1;action_focus=-1;catalog_focus=catalog_top=0;
                snprintf(search,sizeof(search),"%s",saved_search);
                message=reload_saved()?TR(IPTV_SAVE_FAILED):"";
                restore_saved_selection(saved_parent_url,saved_parent_focus,saved_parent_top);
            } else if(action==6) mode=(mode+1)%4;
            if (action >= 0) {
                sceCtrlPeekBufferPositive(0, &previous, 1);
                ui_touch_reset(); ui_nav_repeat_reset(&repeat);
            }
        } else ui_nav_repeat_reset(&repeat);
        filter_catalog();
        char shown[VT_IPTV_URL_MAX]; display_link(shown);
        vita2d_start_drawing(); vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG, VT_THEME_SIGNAL_BRIGHT);
        ui_brand_draw_breadcrumbs(TR(IPTV_TITLE), NULL,
            saved_view ? (starred_view?TR(ONLINE_FAVORITES):TR(IPTV_COLLECTION)) : catalog.count ? current_list.title : TR(IPTV_ADD));
        if(catalog.count || saved_view) {
            ui_action_button(UI_CONTENT_X,UI_TABS_Y,240,40,VT_THEME_SURFACE,"Start",TR(IPTV_ADD),0);
            char count_label[192];snprintf(count_label,sizeof(count_label),"%s  %u/%u",search[0]?search:TR(IPTV_SEARCH_SHORT),result_count,catalog.count);
            ui_action_button(272,UI_TABS_Y,276,40,VT_THEME_SURFACE,"Triangle",count_label,0);
            ui_action_button(564,UI_TABS_Y,176,40,VT_THEME_SURFACE,"",saved_view?(starred_view?TR(UI_ALL):TR(ONLINE_FAVORITES)):hide_unavailable?TR(IPTV_SHOW_ALL_SHORT):TR(IPTV_FILTER_SHORT),saved_view?starred_view:hide_unavailable);
            UiContextAction bar[4];catalog_actions(bar);
            for(int i=0;i<4;i++) {
                ui_action_button(18+i*233,110,225,46,bar[i].enabled?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE,
                    bar[i].detail?bar[i].detail:"",bar[i].label,0);
                if(page_input && action_focus==i)ui_focus_motion_tick(&focus_motion,18+i*233,110,225,46);
            }
            for(int i=0;i<catalog_visible && catalog_top+i<(int)result_count;i++) {
                int x=UI_CONTENT_X+i%2*466,y=IPTV_CATALOG_Y+i/2*catalog_step;VtIptvChannel *item=&catalog.items[results[catalog_top+i]];
                int selected=page_input && action_focus<0 && catalog_top+i==catalog_focus;
                ui_panel(x,y,458,catalog_step-8,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
                if(selected)ui_focus_motion_tick(&focus_motion,x,y,458,catalog_step-8);
                vita2d_texture *cover=item->artwork_path[0] && strcmp(item->artwork_path,"-") ? vt_artwork_thumbnail_get(item->artwork_path) : NULL;
                int art_size=catalog_step-24; if(art_size>92)art_size=92;
                ui_artwork_tile(x+8,y+(catalog_step-8-art_size)/2,art_size,cover,item->mode==VT_IPTV_LIST?"M3U":"TV");
                char title[192];snprintf(title,sizeof(title),"%s%s",item->starred?"* ":"",item->title);
                text_line(x+112,y+38,332,title,VT_THEME_TEXT,UI_FONT_BODY);
                text_line(x+112,y+68,332,item->group[0]?item->group:mode_label(item->mode),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
            }
            if(page_input && (result_count || action_focus>=0))ui_focus_outline_draw(&focus_motion);
            if(!result_count) {
                text_line(68,240,820,search[0]?TR(IPTV_NO_RESULTS):TR(IPTV_NO_SAVED),VT_THEME_TEXT,UI_FONT_BODY);
                text_line(68,274,820,search[0]?TR(IPTV_CLEAR_SEARCH):TR(IPTV_NO_SAVED_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
            }
            ui_scroll_rail(948,IPTV_CATALOG_Y,catalog_rows*catalog_step-8,catalog_top,catalog_visible,result_count);
            text_line(UI_CONTENT_X,ui_mini_player_top()-12,UI_CONTENT_W,message[0]?message:TR(IPTV_MANAGE_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        } else {
        text_line(18,89,720,TR(IPTV_FORMATS),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        ui_panel(18,110,924,90,VT_THEME_SURFACE,VT_THEME_BORDER,0);
        text_line(34,140,892,TR(IPTV_LINK_EDIT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        text_line(34,178,892,link[0]?shown:TR(IPTV_EMPTY),VT_THEME_TEXT,UI_FONT_BODY);
        ui_action_button(18,218,596,52,link[0]?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE,"X",mode==VT_IPTV_LIST?TR(IPTV_OPEN_LIST):TR(IPTV_PLAY),0);
        ui_action_button(630,218,312,52,VT_THEME_SURFACE,"Square",form_link_saved?TR(IPTV_RENAME):TR(IPTV_SAVE),0);
        ui_action_button(18,286,458,52,VT_THEME_SURFACE,"Triangle",TR(IPTV_QR),0);
        ui_action_button(484,286,458,52,VT_THEME_SURFACE,"Circle",TR(IPTV_COLLECTION),0);
        ui_action_button(18,352,924,48,VT_THEME_SURFACE,"Select",mode_label(mode),0);
        if(page_input) {
            ui_focus_motion_tick(&focus_motion,form_rects[focus][0],form_rects[focus][1],form_rects[focus][2],form_rects[focus][3]);
            ui_focus_outline_draw(&focus_motion);
        }
        ui_text_block(18,430,924,24,2,message[0]?message:TR(IPTV_ADD_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        }
        ui_action_button(756,UI_TABS_Y,186,40,VT_THEME_SURFACE,"R1",TR(UI_OPTIONS),options.open);
        ui_mini_player_draw();
        option_count=options_for_page(option_items);
        ui_context_menu_draw(&options,TR(UI_OPTIONS),option_items,option_count);
        if (sidebar.animation > 0.01f) ui_sections_sidebar_draw(&sidebar);
        vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
        sceKernelDelayThread(1000);
    }
}

int ui_iptv_open_library(const VtLibraryItem *v) {
    snprintf(link, sizeof(link), "%s", v->locator);
    snprintf(channel_title, sizeof(channel_title), "%s", v->title);
    snprintf(channel_agent, sizeof(channel_agent), "%s", v->user_agent);
    mode = v->mode;
    memset(&current_list, 0, sizeof(current_list));
    const char *message = NULL;
    int ret = play_link(&message);
    return ret;
}
