#include "ui/artwork_path.h"
#include "network/artwork_cache.h"
#include "network/iptv_catalog_fetch.h"
#include "ui/iptv_screen.h"
#include "ui/folder_picker.h"
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
#include "ui/iptv_layout.h"
#include "ui/scroll_view.h"
#include "ui/list_scroll.h"
#include "network/iptv_quality.h"
#include "ui/page_motion.h"
#include "ui/frame_pacing.h"
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
static int mode,library_parent_active;
static VtIptvCatalog catalog;
static int catalog_focus, catalog_top;
static unsigned results[VT_IPTV_CHANNEL_MAX],result_count;
static char search[128],saved_search[128];
static int filter_dirty=1;
static int hide_unavailable;
static volatile int probe_cancel;
static volatile long probe_current,probe_total;
static char probe_summary[256];
static int saved_view, starred_view, saved_channels, split_view;
static VtIptvChannel list_parents[8];
static unsigned list_parent_count;
static void default_link_name(VtIptvChannel *item);
/* Independent bounded playlist metadata: opening a playlist replaces only
 * the channel catalogue, so both panes can remain visible. Row 0 is saved channels. */
static VtIptvChannel playlist_entries[VT_IPTV_SAVED_MAX+1];
static unsigned playlist_count,saved_link_count;
static char saved_link_keys[VT_IPTV_SAVED_MAX][17];
static int picker_focus,picker_cursor;
static UiScrollView picker_scroll;
static UiFocusMotion picker_motion;
static int saved_parent_focus,saved_parent_top;
static char saved_parent_url[VT_IPTV_URL_MAX];
static unsigned artwork_generation,artwork_worker_generation;
static volatile int catalog_cancel;
static VtIptvChannel current_list;
static int current_list_saved, form_link_saved, list_from_form;
static int form_playlist=1;
#define IPTV_CATALOG_Y (split_view?152:116)
#define IPTV_CATALOG_FOOTER_H 40
static SceUID artwork_thread = -1;
static volatile int artwork_cancel, artwork_done;
static int artwork_index, catalog_visible=6;
static VtOnlineItem artwork_item;
static struct {uint64_t key,after;} artwork_retry[VT_IPTV_CHANNEL_MAX];
#define TR(id) vt_i18n_str(VT_STR_##id)
static const char *failure_detail(int event);
static int channel_columns(void);
static void text_line(int x,int y,int width,const char *text,unsigned color,int size);
static int result_name_compare(const void *left,const void *right){
 unsigned a=*(const unsigned*)left,b=*(const unsigned*)right;
 int order=vt_iptv_channel_compare_name(&catalog.items[a],&catalog.items[b]);
 return order?order:a<b?-1:a>b;
}
static void filter_catalog(void) {
    if(!filter_dirty)return;
    result_count=0;
    for(unsigned i=0;i<catalog.count;i++)if((!saved_view || (saved_channels?!vt_iptv_channel_collection(&catalog.items[i]):vt_iptv_channel_collection(&catalog.items[i]))) &&
        !(split_view&&saved_view&&!saved_channels) && vt_iptv_channel_matches(&catalog.items[i],search) &&
        !(saved_view && starred_view && !catalog.items[i].starred) &&
        !(hide_unavailable && vt_iptv_probe_hidden(catalog.items[i].availability)))results[result_count++]=i;
    if((!saved_view||saved_channels)&&vt_preferences_iptv_name_sort())qsort(results,result_count,sizeof(*results),result_name_compare);
    if(catalog_focus>=(int)result_count)catalog_focus=result_count?result_count-1:0;
    if(catalog_top>catalog_focus)catalog_top=catalog_focus;
    filter_dirty=0;
}
static int change_channel_sort(const char **message){
 filter_catalog();unsigned selected=catalog_focus>=0&&(unsigned)catalog_focus<result_count?results[catalog_focus]:VT_IPTV_CHANNEL_MAX;
 if(vt_preferences_set_iptv_name_sort(!vt_preferences_iptv_name_sort())){*message=TR(IPTV_SAVE_FAILED);return -1;}
 filter_dirty=1;filter_catalog();
 for(unsigned i=0;i<result_count;i++)if(results[i]==selected){catalog_focus=(int)i;break;}
 int columns=channel_columns();catalog_top=(catalog_focus/columns)*columns;
 *message="";return 0;
}
static int channel_saved(const VtIptvChannel *item){
 if(!item)return 0;
 if(item->starred)return 1;
 char key[17];vt_iptv_choice_key(item->url,key);
 for(unsigned i=0;i<saved_link_count;i++)if(!strcmp(key,saved_link_keys[i]))return 1;
 return 0;
}
static void remember_saved_link(const VtIptvChannel *item){
 if(vt_iptv_channel_collection(item))return;
 char key[17];vt_iptv_choice_key(item->url,key);
 for(unsigned i=0;i<saved_link_count;i++)if(!strcmp(key,saved_link_keys[i]))return;
 if(saved_link_count<VT_IPTV_SAVED_MAX)vt_iptv_choice_key(item->url,saved_link_keys[saved_link_count++]);
}
static int refresh_playlists(void){
 VtIptvCatalog *all=calloc(1,sizeof(*all));if(!all)return -1;
 int result=vt_iptv_saved_load(all,0);
 if(!result){
  playlist_count=saved_link_count=0;int found=0;
  for(unsigned i=0;i<all->count&&saved_link_count<VT_IPTV_SAVED_MAX;i++)if(!vt_iptv_channel_collection(&all->items[i]))
   vt_iptv_choice_key(all->items[i].url,saved_link_keys[saved_link_count++]);
  for(unsigned i=0;i<all->count&&playlist_count<VT_IPTV_SAVED_MAX;i++)if(vt_iptv_channel_collection(&all->items[i])){
   playlist_entries[playlist_count++]=all->items[i];
   if(!strcmp(all->items[i].url,current_list.url))found=1;
  }
  if(!found&&current_list.url[0]&&vt_iptv_channel_collection(&current_list))playlist_entries[playlist_count++]=current_list;
  if(picker_cursor>(int)playlist_count)picker_cursor=playlist_count;
 }
 free(all);return result;
}
static int channel_list_x(void){return split_view?340:18;}
static int channel_list_width(void){return split_view?602:924;}
static int channel_columns(void){return saved_view&&!saved_channels?1:split_view?2:3;}
static int channel_step(void){return channel_columns()==1?76:96;}
static int channel_card_width(void){return (channel_list_width()-(channel_columns()-1)*8)/channel_columns();}
static void clamp_channel_focus(int first_row,int last_row){
 int columns=channel_columns(),col=catalog_focus%columns;
 if(catalog_focus<first_row*columns)catalog_focus=first_row*columns+col;
 if(catalog_focus>=(last_row+1)*columns)catalog_focus=last_row*columns+col;
 if(catalog_focus>=(int)result_count)catalog_focus=result_count?result_count-1:0;
}
static VtIptvChannel *selected_channel(void) {
    filter_catalog();
    if(split_view&&picker_focus)return picker_cursor>0&&picker_cursor<=(int)playlist_count?&playlist_entries[picker_cursor-1]:NULL;
    return catalog_focus>=0 && catalog_focus<(int)result_count?&catalog.items[results[catalog_focus]]:NULL;
}
static int reload_saved(void) {
    int result=vt_iptv_saved_load(&catalog,starred_view);hide_unavailable=0;filter_dirty=1;refresh_playlists();return result;
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
    if(catalog_top>catalog_focus)catalog_top=catalog_focus;
}

static char *iptv_local_logo(const char *url,volatile int *cancel,size_t *size){
 *size=0;char path[VT_IPTV_URL_MAX];
 if(!vt_iptv_local_path(url)||vt_iptv_resource_resolve(url,url,path,sizeof(path))||(cancel&&*cancel))return NULL;
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
 if(n<=0||n>3*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
 char *data=malloc((size_t)n);if(!data){fclose(f);return NULL;}
 size_t read=fread(data,1,(size_t)n,f);fclose(f);
 if(read!=(size_t)n||(cancel&&*cancel)){free(data);return NULL;}*size=read;return data;
}
static int iptv_cover_worker(SceSize args, void *ctx) {
    (void)args; (void)ctx;
    if(vt_iptv_local_path(artwork_item.artwork_url))vt_artwork_cache_get(artwork_item.artwork_url,artwork_item.artwork_path,sizeof(artwork_item.artwork_path),&artwork_cancel,iptv_local_logo);
    else vt_online_fetch_cover(&artwork_item, &artwork_cancel);
    __sync_synchronize(); artwork_done = 1; return sceKernelExitThread(0);
}
static void iptv_cover_tick(void) {
    /* Restore every visible cached path before a single network job can wait. */
    for(int i=catalog_top;i<catalog_top+catalog_visible&&i<(int)result_count;i++){
        VtIptvChannel *item=&catalog.items[results[i]];char cached[192];
        if(item->logo[0]&&!vt_artwork_cache_peek(item->logo,cached,sizeof(cached)))
            ui_artwork_path_update(item->artwork_path,sizeof(item->artwork_path),cached);
    }

    if (artwork_thread >= 0 && artwork_done) {
        sceKernelWaitThreadEnd(artwork_thread,NULL,NULL);sceKernelDeleteThread(artwork_thread);artwork_thread=-1;
        if(!artwork_cancel && artwork_worker_generation==artwork_generation && artwork_index<(int)catalog.count && artwork_item.artwork_path[0])
            ui_artwork_path_update(catalog.items[artwork_index].artwork_path,sizeof(catalog.items[0].artwork_path),artwork_item.artwork_path);
    }
    if (artwork_thread >= 0) return;
    for (int i = catalog_top; i < catalog_top+catalog_visible && i < (int)result_count; i++) {
        VtIptvChannel *item = &catalog.items[results[i]];
        if (!item->logo[0] || !vt_artwork_cache_due(item->logo)) continue;
        uint64_t now=sceKernelGetProcessTimeWide(),key=ui_page_key(item->url,item->logo);
        unsigned index=results[i];
        if(artwork_retry[index].key==key&&now<artwork_retry[index].after)continue;
        artwork_retry[index].key=key;artwork_retry[index].after=now+30000000ULL;
        memset(&artwork_item, 0, sizeof(artwork_item));
        snprintf(artwork_item.artwork_url, sizeof(artwork_item.artwork_url), "%s", item->logo);
        snprintf(artwork_item.artwork_path,sizeof(artwork_item.artwork_path),"%s",item->artwork_path[0]=='-'?"":item->artwork_path);
        artwork_index = results[i]; artwork_cancel = artwork_done = 0;artwork_worker_generation=artwork_generation;
        artwork_thread = sceKernelCreateThread("VmdIptvCover", iptv_cover_worker, 0x10000100, 0x40000, 0, 0, NULL);
        if (artwork_thread >= 0 && sceKernelStartThread(artwork_thread, 0, NULL) < 0) {
            sceKernelDeleteThread(artwork_thread); artwork_thread = -1;
        }

        break;
    }
}
static void channel_cached_logo(VtIptvChannel *item){
 char cached[192];if(item->logo[0]&&!vt_artwork_cache_peek(item->logo,cached,sizeof(cached)))
  snprintf(item->artwork_path,sizeof(item->artwork_path),"%s",cached);
}
static void channel_library_metadata(VtIptvChannel *item){
 if(!vt_iptv_channel_generic_name(item)&&item->logo[0]){channel_cached_logo(item);return;}
 VtLibraryItem key;VtLibraryResult known;vt_library_iptv(item,&key);
 if(vt_library_get(key.id,&known)&&known.item.provider==VT_LIBRARY_IPTV&&!strcmp(known.item.locator,item->url)){
  VtIptvChannel metadata={.name_source=VT_IPTV_NAME_METADATA};
  snprintf(metadata.title,sizeof(metadata.title),"%.159s",known.item.title);
  snprintf(metadata.logo,sizeof(metadata.logo),"%s",known.item.artwork_url);
  vt_iptv_channel_merge(item,&metadata);
 }
 channel_cached_logo(item);
}
static int load_catalog(void *ctx) {
    (void)ctx; hide_unavailable=0;unsigned char *data = NULL; size_t size = 0; char effective[VT_IPTV_URL_MAX];
    VtIptvHttpOptions options={.allow_local=vt_iptv_local_path(link)};
    snprintf(options.user_agent,sizeof(options.user_agent),"%s",channel_agent);
    int local_video=vt_iptv_local_path(link)&&vt_iptv_link_kind(link)==VT_IPTV_FILE;
    snprintf(effective,sizeof(effective),"%s",link);
    int result = local_video?0:vt_iptv_catalog_fetch(vt_iptv_http_fetch, &options, link, &data, &size,
        effective, sizeof(effective), &catalog_cancel);
    int fetched=result;int old_kind=current_list.mode;
    if(!result){
        int kind=local_video?VT_IPTV_FILE:vt_iptv_catalog_content_kind(data,size);
        if(kind==VT_IPTV_HLS||kind==VT_IPTV_DASH||kind==VT_IPTV_FILE){
            catalog.count=1;catalog.truncated=0;catalog.items[0]=current_list;catalog.items[0].mode=kind;
            snprintf(catalog.items[0].url,sizeof(catalog.items[0].url),"%s",link);
            vt_iptv_catalog_metadata(data,size,effective,&catalog.items[0]);
            channel_library_metadata(&catalog.items[0]);
            if(vt_iptv_channel_generic_name(&catalog.items[0])){catalog.items[0].title[0]=0;default_link_name(&catalog.items[0]);}
            current_list=catalog.items[0];
        }else {
            result=vt_iptv_catalog_parse(data,size,effective,&catalog);
            if(!result)current_list.mode=VT_IPTV_LIST;
        }
    }
    if(!result&&current_list.mode==VT_IPTV_LIST){
        current_list.single_channel=catalog.count==1&&catalog.items[0].mode!=VT_IPTV_LIST;
        if(current_list.single_channel){
            vt_iptv_channel_merge(&current_list,&catalog.items[0]);channel_library_metadata(&current_list);
            if(vt_iptv_channel_generic_name(&current_list)){
                VtIptvChannel fallback=current_list;fallback.title[0]=0;fallback.mode=VT_IPTV_HLS;default_link_name(&fallback);
                snprintf(current_list.title,sizeof(current_list.title),"%s",fallback.title);current_list.name_source=fallback.name_source;
            }
        }
    }
    log_printf("IPTV catalog: transport=%d parse=%d bytes=%u entries=%u truncated=%d agent=%d\n",
        fetched,fetched?0:result,(unsigned)size,result?0:catalog.count,catalog.truncated,!!channel_agent[0]);
    free(data);
    VtIptvCatalog *saved = !result ? calloc(1,sizeof(*saved)) : NULL;
    current_list_saved=0;
    if(saved && !vt_iptv_saved_load(saved,0)) {
        for(unsigned j=0;j<saved->count;j++)if(!strcmp(current_list.url,saved->items[j].url)) {
            current_list_saved=1;current_list.starred=saved->items[j].starred;current_list.preferred=saved->items[j].preferred;
            if(saved->items[j].name_source==VT_IPTV_NAME_CUSTOM||
               (saved->items[j].name_source==VT_IPTV_NAME_UNKNOWN&&!vt_iptv_channel_generic_name(&saved->items[j]))){
                snprintf(current_list.title,sizeof(current_list.title),"%s",saved->items[j].title);current_list.name_source=saved->items[j].name_source;
            }
            vt_iptv_channel_merge(&current_list,&saved->items[j]);channel_cached_logo(&current_list);
            if(memcmp(&current_list,&saved->items[j],sizeof(current_list))&&
               (old_kind!=current_list.mode||current_list.single_channel!=saved->items[j].single_channel||
                strcmp(current_list.title,saved->items[j].title)||strcmp(current_list.logo,saved->items[j].logo)))
                if(vt_iptv_saved_set(&current_list))log_printf("IPTV identity update could not be saved\n");
            snprintf(current_list.guide_url,sizeof(current_list.guide_url),"%s",saved->items[j].guide_url);
        }
        for(unsigned i=0;i<catalog.count;i++)for(unsigned j=0;j<saved->count;j++)
            if(!strcmp(catalog.items[i].url,saved->items[j].url)){catalog.items[i].starred=saved->items[j].starred;catalog.items[i].preferred=saved->items[j].preferred;}
    }
    if(!result){
        char provider[512];vt_iptv_catalog_provider_guide(current_list.url,provider);
        for(unsigned i=0;i<catalog.count;i++){
            VtIptvChannel *channel=&catalog.items[i];
            if(current_list.guide_url[0]||(!channel->guide_url[0]&&provider[0]))
                snprintf(channel->guide_url,sizeof(channel->guide_url),"%s",current_list.guide_url[0]?current_list.guide_url:provider);
        }
    }
    if(!result&&current_list.mode!=VT_IPTV_LIST)catalog.items[0]=current_list;
    if (!result)
        for (unsigned i = 0; i < catalog.count; i++) {
            channel_library_metadata(&catalog.items[i]);
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
        int old=0;VtIptvChannel updated=*item;
        for(unsigned i=0;i<saved->count;i++)if(!strcmp(saved->items[i].url,item->url)){
            old=saved->items[i].starred;if(!updated.preferred.video_key[0]&&!updated.preferred.height)updated.preferred=saved->items[i].preferred;
        }
        updated.starred=!old;result=vt_iptv_saved_set(&updated);
        if(!result){*item=updated;remember_saved_link(item);}
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
typedef struct {VtIptvChannel *channel;VtIptvOptions *options;const VtIptvPreference *confirmed;const int *have_confirmed;} PlayerFavoriteContext;
static int player_favorite_with_quality(void *ctx,int toggle){
 PlayerFavoriteContext *p=ctx;if(!toggle)return p->channel->starred;
 VtIptvChannel copy=*p->channel;
 if(!copy.starred&&p->confirmed&&p->have_confirmed&&*p->have_confirmed)copy.preferred=*p->confirmed;
 if(toggle_star(&copy))return -1;
 *p->channel=copy;
 for(unsigned i=0;i<catalog.count;i++)if(!strcmp(catalog.items[i].url,copy.url)){
  catalog.items[i].starred=copy.starred;catalog.items[i].preferred=copy.preferred;
 }
 filter_dirty=1;return copy.starred;
}
static void publish_channel_identity(VtIptvChannel *channel,const VtIptvOptions *options,const VtIptvProgram *program){
 VtIptvChannel enriched=*channel,metadata={.name_source=VT_IPTV_NAME_METADATA};
 if(options){
  snprintf(metadata.title,sizeof(metadata.title),"%s",options->channel_name);
  snprintf(metadata.logo,sizeof(metadata.logo),"%s",options->channel_logo);
  snprintf(metadata.channel_id,sizeof(metadata.channel_id),"%s",options->channel_id);
 }
 vt_iptv_channel_merge(&enriched,&metadata);
 if(program){
  snprintf(metadata.title,sizeof(metadata.title),"%s",program->channel_name);
  snprintf(metadata.logo,sizeof(metadata.logo),"%s",program->channel_logo);
  snprintf(metadata.channel_id,sizeof(metadata.channel_id),"%s",program->channel_id);
  vt_iptv_channel_merge(&enriched,&metadata);
 }
 if(!memcmp(&enriched,channel,sizeof(enriched)))return;
 channel_cached_logo(&enriched);
 if(channel_saved(channel)||(current_list_saved&&!strcmp(current_list.url,channel->url)))
  if(vt_iptv_saved_set(&enriched))ui_message_show(TR(IPTV_TITLE),TR(IPTV_SAVE_FAILED),3000);
 *channel=enriched;
 if(!strcmp(link,channel->url))snprintf(channel_title,sizeof(channel_title),"%s",channel->title);
 if(!strcmp(current_list.url,channel->url))current_list=enriched;
 for(unsigned i=0;i<catalog.count;i++)if(!strcmp(catalog.items[i].url,channel->url)){
  vt_iptv_channel_merge(&catalog.items[i],channel);channel_cached_logo(&catalog.items[i]);
 }
 if(current_list.single_channel&&catalog.count==1&&!strcmp(catalog.items[0].url,channel->url)&&vt_iptv_channel_merge(&current_list,channel)){
  channel_cached_logo(&current_list);
  if(current_list_saved&&vt_iptv_saved_set(&current_list))log_printf("IPTV single-channel identity save failed\n");
  VtLibraryItem wrapper;vt_library_iptv(&current_list,&wrapper);vt_library_cache(&wrapper);
 }
 VtLibraryItem item;vt_library_iptv(channel,&item);vt_library_cache(&item);filter_dirty=1;
}
static void player_identity_metadata(void *opaque,const VtIptvProgram *program){
 PlayerFavoriteContext *context=opaque;publish_channel_identity(context->channel,context->options,program);
}
static int player_saved_channel(void *ctx,int toggle){
 PlayerFavoriteContext *p=ctx;int exists=channel_saved(p->channel);if(!toggle)return exists;
 VtIptvChannel copy=*p->channel;int result;
 if(exists)result=vt_iptv_saved_remove(copy.url);
 else{
  copy.starred=0;if(p->confirmed&&p->have_confirmed&&*p->have_confirmed)copy.preferred=*p->confirmed;
  result=vt_iptv_saved_set(&copy);
 }
 if(result)return -1;
 if(exists)copy.starred=0;
 *p->channel=copy;
 for(unsigned i=0;i<catalog.count;i++)if(!strcmp(catalog.items[i].url,copy.url))catalog.items[i].starred=copy.starred;
 if(saved_view)reload_saved();else refresh_playlists();
 filter_dirty=1;log_printf("IPTV player saved membership: saved=%d\n",!exists);return !exists;
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
            if(!item->preferred.video_key[0]&&!item->preferred.height)item->preferred=saved->items[i].preferred;
            if(!item->logo[0])snprintf(item->logo,sizeof(item->logo),"%s",saved->items[i].logo);
            if(!item->user_agent[0])snprintf(item->user_agent,sizeof(item->user_agent),"%s",saved->items[i].user_agent);
            if(!item->group[0])snprintf(item->group,sizeof(item->group),"%s",saved->items[i].group);
            if(!item->channel_id[0])snprintf(item->channel_id,sizeof(item->channel_id),"%s",saved->items[i].channel_id);
            if(!item->guide_name[0])snprintf(item->guide_name,sizeof(item->guide_name),"%s",saved->items[i].guide_name);
            break;
        }
        result=vt_iptv_saved_set(item);
    }
    free(saved);if(!result)remember_saved_link(item);return result;
}
/* Work on a copy: cancelled/invalid input or a failed write must not rename
 * the visible entry, lose its starred state, or create a second saved URL. */
static int prompt_link_name(VtIptvChannel *item, const char **message) {
    if (!item || !item->url[0]) return 0;
    char input[sizeof(item->title)];
    if (ui_text_input(vt_iptv_channel_collection(item)?TR(IPTV_LIST_NAME):TR(IPTV_NAME),
                      item->title,input,sizeof(input)) <= 0) return 0;
    input[sizeof(input)-1]=0;
    char *start=input; while (*start==' ' || *start=='\t') start++;
    size_t length=strlen(start);
    while(length && (start[length-1]==' ' || start[length-1]=='\t'))length--;
    int valid=length>0;
    for(size_t i=0;i<length;i++)if((unsigned char)start[i]<32 || start[i]==127)valid=0;
    if(!valid){*message=TR(IPTV_NAME_INVALID);return -1;}
    memcpy(item->title,start,length);item->title[length]=0;item->name_source=VT_IPTV_NAME_CUSTOM;
    return 1;
}
static int rename_saved_link(VtIptvChannel *item, const char **message) {
    if (!item || !item->url[0]) return 0;
    VtIptvChannel renamed = *item;
    int result=prompt_link_name(&renamed,message);
    if(result<=0)return result;
    if(save_link(&renamed)){*message=TR(IPTV_SAVE_FAILED);return -1;}
    *item=renamed;
    filter_dirty=1;
    if(!strcmp(current_list.url,item->url)){current_list=renamed;current_list_saved=1;}
    if(!strcmp(link,item->url)){snprintf(channel_title,sizeof(channel_title),"%s",renamed.title);form_link_saved=1;}
    refresh_playlists();*message=TR(IPTV_SAVED);return 1;
}
/* Adding never opens a second text dialog. Preserve a previously saved name;
 * a new link receives a readable host-based title and can be renamed later. */
static void default_link_name(VtIptvChannel *item) {
    if(item->title[0])return;
    VtIptvCatalog *saved=calloc(1,sizeof(*saved));
    if(saved&&!vt_iptv_saved_load(saved,0))for(unsigned i=0;i<saved->count;i++)
        if(!strcmp(saved->items[i].url,item->url)&&!vt_iptv_channel_generic_name(&saved->items[i])){
            snprintf(item->title,sizeof(item->title),"%s",saved->items[i].title);item->name_source=saved->items[i].name_source;break;
        }
    free(saved);
    if(item->title[0])return;
    const char *host=strstr(item->url,"://");host=host?host+3:item->url;
    if(vt_iptv_local_path(item->url)){const char *file=strrchr(item->url,'/');host=file?file+1:strchr(item->url,':')+1;}
    char name[100];size_t length=strcspn(host,"/?#");if(length>=sizeof(name))length=sizeof(name)-1;
    memcpy(name,host,length);name[length]=0;
    snprintf(item->title,sizeof(item->title),item->mode==VT_IPTV_LIST?TR(IPTV_DEFAULT_LIST_NAME):TR(IPTV_DEFAULT_CHANNEL_NAME),name);item->name_source=VT_IPTV_NAME_AUTO;
}
static int open_list(const char **message) {
    if(saved_view)snprintf(saved_search,sizeof(saved_search),"%s",search);
    char checked[VT_IPTV_URL_MAX];
    int valid=vt_iptv_local_path(link)?vt_iptv_resource_resolve(link,link,checked,sizeof(checked)):vt_iptv_input_url(link,strlen(link),checked,sizeof(checked));
    if(valid){*message=valid==VT_IPTV_LIMIT?TR(IPTV_TOO_LARGE):TR(IPTV_INVALID);return valid;}
    snprintf(link,sizeof(link),"%s",checked);
    search[0]=0;filter_dirty=1;
    iptv_cover_stop();catalog_cancel=0;saved_view=0;
    int result=ui_loading_run(TR(IPTV_MODE_LIST),load_catalog,NULL,&catalog_cancel,NULL,NULL);
    int empty=!result&&!catalog.count;
    if(empty)result=VT_IPTV_INVALID;
    if(result<0){
        catalog.count=0;
        *message=empty?TR(IPTV_EMPTY_LIST):result==VT_IPTV_CANCELLED?"":result==VT_IPTV_INVALID || result==VT_IPTV_UNSUPPORTED
            ?TR(IPTV_LIST_INVALID):result==VT_IPTV_LIMIT?TR(IPTV_CATALOG_TOO_LARGE):failure_detail(result);
        if(result!=VT_IPTV_CANCELLED)ui_message_show(TR(IPTV_LIST_FAILED),*message,12000);
    } else {
        if(current_list.mode!=VT_IPTV_LIST){
            VtIptvChannel automatic=current_list;automatic.title[0]=0;automatic.mode=VT_IPTV_LIST;default_link_name(&automatic);
            if(!strcmp(current_list.title,automatic.title)||vt_iptv_channel_generic_name(&current_list))current_list.title[0]=0;
            default_link_name(&current_list);catalog.items[0]=current_list;
            snprintf(link,sizeof(link),"%s",current_list.url);snprintf(channel_title,sizeof(channel_title),"%s",current_list.title);
            mode=current_list.mode;form_playlist=0;
        }
        *message=catalog.truncated?TR(IPTV_LIST_LIMIT):"";
    }
    log_save(VITAMEDIADECK_SESSION_LOG_PATH);
    catalog_focus=catalog_top=0;
    refresh_playlists();return result;
}

/* A fresh input edge and Cancel-first focus prevent the menu's held X from
 * removing an entry. Only the saved reference is deleted, not its contents. */
static int confirm_channel_action(const VtIptvChannel *item,int retry,const char *detail) {
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
        ui_brand_draw_breadcrumbs(TR(IPTV_TITLE),NULL,retry?TR(IPTV_QUALITY):TR(IPTV_DELETE));
        ui_modal_panel(174,96,612,350);
        text_line(200,140,560,retry?TR(IPTV_FAILED):TR(IPTV_DELETE_TITLE),VT_THEME_TEXT,UI_FONT_BODY);
        text_line(200,182,560,item->title,VT_THEME_TEXT,UI_FONT_BODY);
        ui_text_block(200,224,560,25,5,retry?detail:TR(IPTV_DELETE_DETAIL),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        ui_action_button(200,350,275,50,VT_THEME_SURFACE_RAISED,"",TR(NETWORK_CANCEL),0);
        ui_action_button(485,350,275,50,VT_THEME_SURFACE_RAISED,"",retry?TR(IPTV_RETRY_AUTOMATIC):TR(IPTV_DELETE),0);
        text_line(200,430,560,TR(UI_SELECTION_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        ui_focus_motion_tick(&motion,200+selected*285,350,275,50);ui_focus_outline_draw(&motion);
        vita2d_end_drawing();vita2d_wait_rendering_done();vita2d_swap_buffers();sceKernelDelayThread(1000);
    }
}
static int confirm_remove(const VtIptvChannel *item){return confirm_channel_action(item,0,NULL);}
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
    catalog.count=0;saved_view=starred_view=saved_channels=0;picker_focus=0;form_playlist=1;mode=VT_IPTV_LIST;
    current_list_saved=form_link_saved=list_from_form=0;list_parent_count=0;
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
    case VT_IPTV_LIMIT: return TR(IPTV_TOO_LARGE);
    case VT_IPTV_MEMORY: return TR(IPTV_MEMORY_ERROR);
    case VT_IPTV_CATALOG_BODY_LIMIT: return TR(IPTV_CATALOG_TOO_LARGE);
    case VT_IPTV_CATALOG_LINE_LIMIT: return TR(IPTV_CATALOG_LINE_TOO_LONG);
    case VT_IPTV_INVALID: return TR(IPTV_MANIFEST_ERROR);
    case UI_LOADING_TASK_FAILED: return TR(IPTV_BACKGROUND_ERROR);
    case VT_IPTV_MANIFEST: return TR(IPTV_MANIFEST_ERROR);
    case VT_IPTV_TIMESTAMPS: return TR(IPTV_TIMESTAMPS_ERROR);
    case VT_IPTV_REMUX: return TR(IPTV_REMUX_ERROR);
    case VT_IPTV_MEDIA_DATA: return TR(IPTV_MEDIA_DATA_ERROR);
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
typedef struct {
 const char *url;int dash;VtIptvSourceConfig *config;VtIptvOptions *out;volatile int *cancel;
} OptionsTask;
static int load_playback_options(void *ctx){OptionsTask *t=ctx;return vt_iptv_options_load(t->url,t->dash,t->config,t->out,t->cancel);}
typedef struct {VtIptvChannel channel;char playlist[VT_IPTV_URL_MAX],language[8];} ProgramContext;
static int load_player_program(void *ctx,VtIptvProgram *out,volatile int *cancel,int refresh){
 ProgramContext *program=ctx;VtIptvChannel *channel=&program->channel;VtIptvHttpOptions http={.allow_local=vt_iptv_local_path(program->playlist)};
 snprintf(http.user_agent,sizeof(http.user_agent),"%s",channel->user_agent);
 VtIptvSourceConfig config={.fetch_ctx=&http};
 /* A recent-item entry retains its parent playlist in the library journal.
  * Recover missing EPG metadata on this loading worker, never during playback
  * startup or the render loop. Normal catalogue opens already have it. */
 if(!channel->guide_url[0]&&!channel->channel_id[0]&&program->playlist[0]){
  unsigned char *data=NULL;size_t size=0;char effective[VT_IPTV_URL_MAX];
  int r=vt_iptv_catalog_fetch(vt_iptv_http_fetch,&http,program->playlist,&data,&size,effective,sizeof(effective),cancel);
  VtIptvCatalog *list=!r?calloc(1,sizeof(*list)):NULL;
  if(list&&!vt_iptv_catalog_parse(data,size,effective,list))for(unsigned i=0;i<list->count;i++)if(!strcmp(list->items[i].url,channel->url)){
   VtIptvChannel *v=&list->items[i];
   snprintf(channel->guide_url,sizeof(channel->guide_url),"%s",v->guide_url);
   snprintf(channel->channel_id,sizeof(channel->channel_id),"%s",v->channel_id);
   snprintf(channel->guide_name,sizeof(channel->guide_name),"%s",v->guide_name);channel->guide_shift=v->guide_shift;break;
  }
  free(list);free(data);
  if(cancel&&__atomic_load_n(cancel,__ATOMIC_ACQUIRE))return VT_IPTV_CANCELLED;
 }
 VtIptvGuideQuery query={.url=channel->guide_url,.channel=channel->channel_id,.name=channel->guide_name,.title=channel->title,.shift_seconds=channel->guide_shift,.refresh=refresh};
 return vt_iptv_program_load_channel(&query,&config,
  program->language,out,cancel);
}
typedef struct {
    VtIptvChannel *channel;const VtIptvOptions *options;const VtIptvSelection *selection;
    VtIptvPreference *remembered;int *have_remembered;
    char identity[17];int replace,migrate,committed;
} PlaybackCommit;
static int commit_playback_started(void *opaque,uint32_t width,uint32_t height){
    PlaybackCommit *commit=opaque;
    publish_channel_identity(commit->channel,commit->options,NULL);
    if((!commit->replace&&!commit->migrate)||commit->committed)return 0;
    VtIptvPreference actual;
    vt_iptv_preference_confirmed(commit->options,commit->selection,width<=16384?width:0,height<=16384?height:0,&actual);
    if(!commit->replace){
        VtIptvPreference previous=*commit->remembered;
        if(!previous.height&&previous.video_key[0]&&!strcmp(previous.video_key,actual.video_key)){
            previous.width=actual.width;previous.height=actual.height;previous.bandwidth=actual.bandwidth;
        }
        actual=previous; /* Identity migration never replaces an unavailable preference with a fallback. */
    }
    if(vt_iptv_playback_save_channel(commit->channel->url,commit->identity,&actual)){
        log_printf("IPTV: confirmed playback preference save failed");return -1;
    }
    *commit->remembered=actual;*commit->have_remembered=1;commit->committed=1;
    log_printf("IPTV preference committed after presented playback: width=%u height=%u language=%d\n",actual.width,actual.height,!!actual.audio_language[0]);
    return 0;
}
static int play_segmented_session(VtIptvChannel *playing,VtIptvOptions *available,VtIptvSelection *selection,
 VtDecoderBackend *backend,VtIptvQualityTrial *trial,const char *history,int *event,VtIptvSourceStatus *status,PlaybackCommit *commit){
 VtIptvSourceConfig config={.buffer_limit=24u*1024u*1024u,.segment_limit=16u*1024u*1024u,
  .selection=*selection,.stream_options=available,.startup_segments=6,.fetch_range=vt_iptv_http_fetch_range};
 VtIptvHttpOptions http={.allow_local=vt_iptv_local_path(playing->url)};snprintf(http.user_agent,sizeof(http.user_agent),"%s",playing->user_agent);config.fetch_ctx=&http;
 if(playing->mode==VT_IPTV_DASH)vt_dash_source_configure(&config);else vt_hls_source_configure(&config);
 VtIptvSession *session=vt_iptv_session_create(playing->url,&config);
 memset(status,0,sizeof(*status));*event=0;if(!session){status->last_error=VT_IPTV_MEMORY;return -1;}
 PlayerFavoriteContext favorite={playing,available,commit?commit->remembered:NULL,commit?commit->have_remembered:NULL};VtHwPlayerScreenSource source={0};
 source.favorite=trial?NULL:player_favorite_with_quality;source.favorite_ctx=trial?NULL:&favorite;
 source.saved=trial?NULL:player_saved_channel;source.saved_ctx=&favorite;
 source.iptv_options=available;source.iptv_selection=selection;source.iptv_backend=backend;source.quality_trial=trial;
 ProgramContext program={.channel=*playing};
 if(!saved_view||library_parent_active)snprintf(program.playlist,sizeof(program.playlist),"%s",current_list.url);
 snprintf(program.language,sizeof(program.language),"%s",vt_i18n_language_code());
 source.iptv_metadata=trial?NULL:player_identity_metadata;source.iptv_metadata_ctx=&favorite;
 source.iptv_program=load_player_program;source.iptv_program_ctx=&program;source.iptv_program_ctx_size=sizeof(program);
 source.iptv_started=commit?commit_playback_started:NULL;source.iptv_started_ctx=commit;
 vt_iptv_session_factory(session,&source.stream);source.title=playing->title;
 source.location=current_list.title[0]?current_list.title:TR(IPTV_TITLE);source.authenticated_remote=1;source.history_id=trial?NULL:history;
 int result=vt_hw_player_screen_run_live(&source,session);*event=vt_iptv_session_event(session);vt_iptv_session_status(session,status);
 vt_iptv_session_cancel(session);UiPlayerLoadingInfo closing={.title=source.title,.status=TR(IPTV_CLOSING)};
 while(!vt_iptv_session_stopped(session)){ui_player_loading_present(&closing,NULL,NULL);sceKernelDelayThread(16000);}
 vt_iptv_session_destroy(session);if(!trial)publish_channel_identity(playing,available,NULL);return result;
}
static unsigned playback_quality_context(void){
 return (1u<<28)|(unsigned)vt_preferences_video_decoder()|((unsigned)vt_preferences_decoder_cpu_mhz(1)<<3)|
  ((unsigned)vt_preferences_decoder_cpu_mhz(0)<<13)|((unsigned)vt_preferences_clock_source()<<23);
}
/* Applying an existing result never opens a decoder or samples playback.
 * New measurements are reachable only from the explicit R1 action. */
static int restore_measured_quality(const char *url,VtIptvOptions *o,VtIptvSelection *selection,VtIptvPreference *recommended){
 if(!o)return 0;
 char signature[17];vt_iptv_quality_signature(o,signature);
 return vt_iptv_quality_cached(url,signature,playback_quality_context(),recommended)&&
        vt_iptv_preference_apply(o,recommended,selection,0);
}
static int choose_smooth_quality(VtIptvChannel *playing,VtIptvOptions *o,VtIptvSelection *selection,
 VtIptvPreference *recommended,int force){
 if(!o)return 0;
 char signature[17];vt_iptv_quality_signature(o,signature);unsigned context=playback_quality_context();
 if(!force&&vt_iptv_quality_cached(playing->url,signature,context,recommended)&&vt_iptv_preference_apply(o,recommended,selection,0))return 1;
 int total=0;for(unsigned i=0;i<o->video_count;i++)if(o->video[i].supported)total++;
 if(total<2)return 0;
 VtIptvQualityReport best={0};VtIptvPreference chosen={0};uint64_t pixels=0;int tested=0,completed=1;
 for(unsigned i=0;i<o->video_count;i++)if(o->video[i].supported){
  VtIptvSelection candidate={0};snprintf(candidate.video,sizeof(candidate.video),"%s",o->video[i].id);
  VtDecoderBackend backend=VT_DECODER_BACKEND_NONE;VtIptvQualityTrial trial={.candidate=++tested,.total=total};
  VtIptvSourceStatus status;int event=0;
  int result=play_segmented_session(playing,o,&candidate,&backend,&trial,NULL,&event,&status,NULL);
  if(result==VT_HW_PLAYER_ACTION_TRIAL_SKIP||trial.skipped||(result==0&&event==VT_IPTV_CANCELLED)){completed=0;break;}
  uint64_t area=(uint64_t)o->active_width*o->active_height;
  log_printf("IPTV quality sample: variant=%u width=%u height=%u HW=%d valid=%d media=%llu wall=%llu freeze=%llu cadence=%u cost=%.2f result=%d\n",
   i,o->active_width,o->active_height,trial.report.hardware,trial.report.valid,
   (unsigned long long)trial.report.media_us,(unsigned long long)trial.report.elapsed_us,
   (unsigned long long)trial.report.stall_us,trial.report.cadence_breaks,vt_iptv_quality_cost(&trial.report),result);
  if(result==VT_HW_PLAYER_ACTION_TRIAL_DONE&&trial.report.valid&&
     (!best.valid||vt_iptv_quality_better(&trial.report,area,&best,pixels))){
   best=trial.report;pixels=area;vt_iptv_preference_capture(o,&chosen);
  }
 }
 if(best.valid){*recommended=chosen;vt_iptv_preference_apply(o,&chosen,selection,0);
  if(completed)vt_iptv_quality_save(playing->url,signature,context,&chosen,&best);
  return 1;}
 return 0;
}
static int push_list_parent(const char *child,const char **message){
 if(saved_view||!current_list.url[0]||!strcmp(current_list.url,child))return 1;
 if(list_parent_count==8){*message=TR(IPTV_NESTED_LIMIT);return 0;}
 for(unsigned i=0;i<list_parent_count;i++)if(!strcmp(list_parents[i].url,child)){*message=TR(IPTV_NESTED_LIMIT);return 0;}
 list_parents[list_parent_count++]=current_list;return 1;
}
static int play_link(const char **message) {
    char checked[VT_IPTV_URL_MAX];
    if (vt_iptv_resource_resolve(link, link, checked, sizeof(checked))) {
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
            playing.starred=saved_items->items[i].starred;playing.preferred=saved_items->items[i].preferred;
            playing.name_source=saved_items->items[i].name_source;
            if(!vt_iptv_channel_generic_name(&saved_items->items[i]))snprintf(playing.title,sizeof(playing.title),"%s",saved_items->items[i].title);
            if(saved_items->items[i].guide_url[0])snprintf(playing.guide_url,sizeof(playing.guide_url),"%s",saved_items->items[i].guide_url);
            if(!playing.channel_id[0])snprintf(playing.channel_id,sizeof(playing.channel_id),"%s",saved_items->items[i].channel_id);
            if(!playing.guide_name[0])snprintf(playing.guide_name,sizeof(playing.guide_name),"%s",saved_items->items[i].guide_name);
            if(!playing.logo[0])snprintf(playing.logo,sizeof(playing.logo),"%s",saved_items->items[i].logo);
            break;
        }
    free(saved_items);
    if((!saved_view||library_parent_active)&&current_list.guide_url[0])snprintf(playing.guide_url,sizeof(playing.guide_url),"%s",current_list.guide_url);
    channel_library_metadata(&playing);
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
        PlayerFavoriteContext membership={.channel=&playing};source.saved=player_saved_channel;source.saved_ctx=&membership;
        source.favorite=player_favorite;source.favorite_ctx=&playing;
        source.history_id = library_item.history_id;
        source.start_position_ms = vt_library_resume(&library_item);
        if(vt_iptv_local_path(link))vt_decoder_file_stream_factory(link,&source.stream);else vt_http_audio_stream_factory(link, &source.stream);
        source.title = playing.title;
        source.location = TR(IPTV_MODE_FILE); source.authenticated_remote = 1;
        int result = vt_hw_player_screen_run(&source, NULL, NULL, NULL, NULL, NULL);
        *message = result < 0 ? TR(IPTV_DECODE_ERROR) : TR(IPTV_EXIT_NOTE);
        if (result < 0) ui_message_show(TR(IPTV_FAILED),*message,12000);
        if (result < 0) { log_printf("IPTV VOD file: result=%d\n", result); log_save(VITAMEDIADECK_SESSION_LOG_PATH); }
        return result >= VT_HW_PLAYER_ACTION_SECTION_BASE && result < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT
            ? result - VT_HW_PLAYER_ACTION_SECTION_BASE : UI_SECTION_NONE;
    }
    VtIptvOptions *available=calloc(1,sizeof(*available));
    VtIptvSelection selection={0},previous_selection={0};int changing=0;
    VtIptvHttpOptions discovery_http={.allow_local=vt_iptv_local_path(link)};snprintf(discovery_http.user_agent,sizeof(discovery_http.user_agent),"%s",channel_agent);
    VtIptvSourceConfig discovery_config={.fetch_ctx=&discovery_http};volatile int discovery_cancel=0;
    if(available){OptionsTask task={link,mode==VT_IPTV_DASH,&discovery_config,available,&discovery_cancel};
        int discovered=ui_loading_run(TR(IPTV_LOADING_OPTIONS),load_playback_options,&task,&discovery_cancel,NULL,NULL);
        if(discovered==VT_IPTV_OPTIONS_DIRECTORY){
            if(!push_list_parent(playing.url,message)){free(available);return UI_SECTION_NONE;}
            current_list=playing;current_list.mode=VT_IPTV_LIST;mode=VT_IPTV_LIST;list_from_form=0;
            open_list(message);free(available);return UI_SECTION_NONE;
        }
        if(available->content_kind>0&&available->content_kind!=VT_IPTV_LIST+1)playing.mode=mode=available->content_kind-1;
        publish_channel_identity(&playing,available,NULL);
        if(mode==VT_IPTV_FILE){free(available);return play_link(message);}
        if(discovery_cancel){free(available);*message=TR(IPTV_CANCELLED);return UI_SECTION_NONE;}
    }
    VtIptvPreference recommended={0},remembered={0};
    char playback_identity[17];vt_iptv_playback_identity(current_list.url,playing.channel_id,playback_identity);
    int lookup=vt_iptv_playback_load_channel(playing.url,playback_identity,&remembered);
    int have_remembered=lookup>0,migrate=lookup>1&&playback_identity[0];
    unsigned restored=vt_iptv_playback_apply(available,have_remembered?&remembered:&playing.preferred,&selection);
    if(!(restored&1)){
        if((have_remembered?remembered.video_key:playing.preferred.video_key)[0])ui_message_show(TR(IPTV_QUALITY),TR(IPTV_PREFERRED_MISSING),2500);
        char language_choice[sizeof(selection.audio)];snprintf(language_choice,sizeof(language_choice),"%s",selection.audio);
        restore_measured_quality(playing.url,available,&selection,&recommended);
        if(restored&2){
            selection.audio[0]=0;
            for(unsigned a=0;available&&a<available->audio_count;a++)
                if(!strcmp(available->audio[a].id,language_choice)&&vt_iptv_audio_matches(available,&selection,a))
                    snprintf(selection.audio,sizeof(selection.audio),"%s",language_choice);
        }
    }
    VtDecoderBackend backend=VT_DECODER_BACKEND_NONE,previous_backend=VT_DECODER_BACKEND_NONE;
    int default_recovery=0;
    for (;;) {
        VtIptvSelection before=selection;VtDecoderBackend backend_before=backend;
        VtIptvPreference last_good=remembered,last_favourite=playing.preferred;int had_good=have_remembered;
        int event=0;VtIptvSourceStatus status;
        PlaybackCommit commit={.channel=&playing,.options=available,.selection=&selection,.remembered=&remembered,.have_remembered=&have_remembered,.replace=!have_remembered||changing||default_recovery,.migrate=migrate};
        snprintf(commit.identity,sizeof(commit.identity),"%s",playback_identity);
        int result=play_segmented_session(&playing,available,&selection,&backend,NULL,library_item.history_id,&event,&status,&commit);
        if(commit.committed)migrate=0;
        log_printf("IPTV playback: result=%d source=%d generation=%llu segments=%u bytes=%u\n",
            result,event,(unsigned long long)status.generation,status.buffered_segments,(unsigned)status.buffered_bytes);
        int cause=event&&event!=VT_IPTV_CANCELLED?event:status.last_error&&status.last_error!=VT_IPTV_CANCELLED?status.last_error:!status.buffered_segments?VT_IPTV_IO:0;
        if(result<0&&commit.committed){
            int undo=had_good?vt_iptv_playback_save_channel(playing.url,playback_identity,&last_good):vt_iptv_playback_remove_channel(playing.url,playback_identity);
            if(memcmp(&playing.preferred,&last_favourite,sizeof(last_favourite))&&channel_saved(&playing)){
                playing.preferred=last_favourite;if(vt_iptv_saved_set(&playing))undo=-1;
            }
            if(!undo){remembered=last_good;have_remembered=had_good;}
            else ui_message_show(TR(IPTV_QUALITY),TR(IPTV_PREFERENCE_SAVE_FAILED),4000);
            log_printf("IPTV preference rollback after playback error: result=%d had_previous=%d\n",undo,had_good);
        }
        if(result<0&&changing){
            selection=previous_selection;backend=previous_backend;changing=0;
            char detail[768];snprintf(detail,sizeof(detail),"%s\n%s",failure_detail(cause),TR(IPTV_FORMAT_RETAINED));
            ui_message_show(TR(IPTV_FAILED),detail,4200);continue;
        }
        changing=0;
        if (result < 0) {
            *message = failure_detail(cause);
            log_save(VITAMEDIADECK_SESSION_LOG_PATH);
            int format_error=cause==0||cause==VT_IPTV_UNSUPPORTED||cause==VT_IPTV_INVALID||cause==VT_IPTV_LIMIT||cause==VT_IPTV_MEMORY||cause==VT_IPTV_NOT_FOUND||
                (cause<=VT_IPTV_MANIFEST&&cause>=VT_IPTV_AUDIO_FORMAT)||cause==VT_IPTV_TIMESTAMPS||cause==VT_IPTV_REMUX||cause==VT_IPTV_MEDIA_DATA;
            if(!default_recovery&&before.video_manual&&format_error){
                char detail[1024];snprintf(detail,sizeof(detail),"%s\n%s",*message,TR(IPTV_RETRY_DEFAULT_DETAIL));
                if(confirm_channel_action(&playing,1,detail)){
                    default_recovery=1;memset(&selection,0,sizeof(selection));backend=VT_DECODER_BACKEND_NONE;
                    log_printf("IPTV: user requested source-default recovery after saved-quality failure\n");continue;
                }
            }
            /* A short startup rejection must not look like an unexplained
             * player exit. Keep the reason in the footer after the notice. */
            ui_message_show(TR(IPTV_FAILED), *message, 12000);
        } else if(event==VT_IPTV_CANCELLED) *message=TR(IPTV_CANCELLED);
        else if (status.terminal == VT_IPTV_END) *message = TR(IPTV_FINISHED);
        else *message = TR(IPTV_EXIT_NOTE);
        if(result==VT_HW_PLAYER_ACTION_RECALIBRATE){
            VtIptvSelection measured={0};
            if(choose_smooth_quality(&playing,available,&measured,&recommended,1)){
                previous_selection=selection;previous_backend=backend;selection=measured;changing=1;
            }
            continue;
        }
        if(result==VT_HW_PLAYER_ACTION_STREAM_CHANGE){
            previous_selection=before;previous_backend=backend_before;changing=1;
            if(!selection.video_manual&&!selection.video[0]){
                if(!vt_iptv_preference_apply(available,&recommended,&selection,0))restore_measured_quality(playing.url,available,&selection,&recommended);
            }
            continue;
        }
        if (result == VT_HW_PLAYER_ACTION_GO_LIVE) continue;
        free(available);
        if (result >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
            result < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT)
            return result - VT_HW_PLAYER_ACTION_SECTION_BASE;
        return UI_SECTION_NONE;
    }
}
static int import_local_file(const char **message){
 static char last_directory[512]="ux0:";char selected[512];
 if(ui_playlist_file_picker(last_directory,selected,sizeof(selected))<=0)return UI_SECTION_NONE;
 snprintf(last_directory,sizeof(last_directory),"%s",selected);char *slash=strrchr(last_directory,'/');
 if(slash)*slash=0;else {char *colon=strchr(last_directory,':');if(colon)colon[1]=0;}
 new_link();snprintf(link,sizeof(link),"%s",selected);current_list.mode=VT_IPTV_LIST;current_list.name_source=VT_IPTV_NAME_AUTO;
 snprintf(current_list.url,sizeof(current_list.url),"%s",selected);
 const char *name=strrchr(selected,'/');snprintf(current_list.title,sizeof(current_list.title),"%s",name?name+1:selected);
 if(open_list(message))return UI_SECTION_NONE;
 if(save_link(&current_list)){*message=TR(IPTV_SAVE_FAILED);return UI_SECTION_NONE;}
 if(current_list.mode!=VT_IPTV_LIST)return play_link(message);
 return UI_SECTION_NONE;
}
static void text_line(int x, int y, int width, const char *text, unsigned color, int size) {
    /* Captions inside native panels must be native too: immediate text is
     * painted before PAF and disappears beneath the panel at presentation. */
    ui_text_line(x,y,width,text,color,(unsigned)size);
}
static const char *channel_type_label(const VtIptvChannel *item){
 return vt_iptv_channel_collection(item)?TR(IPTV_MODE_LIST):TR(IPTV_SINGLE_CHANNEL);
}
static const char *mode_label(int kind) {
    return vt_i18n_str(kind==VT_IPTV_DASH?VT_STR_IPTV_MODE_DASH:kind==VT_IPTV_FILE?VT_STR_IPTV_MODE_FILE:
        kind==VT_IPTV_LIST?VT_STR_IPTV_MODE_LIST:VT_STR_IPTV_MODE_HLS);
}
static void draw_link_field(const char *shown) {
    ui_panel(18,110,924,90,VT_THEME_SURFACE,VT_THEME_BORDER,0);
    text_line(34,140,650,vt_iptv_local_path(link)?TR(IPTV_IMPORT_FILE):form_playlist?TR(IPTV_LIST_URL_LABEL):TR(IPTV_LINK),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
    text_line(702,140,224,vt_iptv_local_path(link)?TR(IPTV_IMPORT_FILE):link[0]?TR(IPTV_LINK_FIELD_EDIT):TR(IPTV_LINK_FIELD_ENTER),VT_THEME_SIGNAL_BRIGHT,UI_FONT_SMALL);
    text_line(34,178,892,link[0]?shown:TR(IPTV_LIST_URL_EMPTY),VT_THEME_TEXT,UI_FONT_BODY);
    vita2d_draw_rectangle(34,188,892,1,VT_THEME_BORDER);
}
static VtIptvChannel *guide_target(void){
 if(!saved_view&&!picker_focus&&current_list.url[0])return &current_list;
 return selected_channel();
}
static int edit_guide(int reset,const char **message){
 VtIptvChannel *target=guide_target();if(!target)return 0;
 VtIptvChannel changed=*target;
 if(reset)changed.guide_url[0]=0;
 else{
  char value[512],checked[512];
  const char *initial=changed.guide_url[0]?changed.guide_url:VT_IPTV_PUBLIC_ITALY_GUIDE;
  int r=ui_text_input(TR(IPTV_GUIDE_URL),initial,value,sizeof(value));if(r<=0)return r;
  if(vt_iptv_input_url(value,strlen(value),checked,sizeof(checked))){*message=TR(IPTV_INVALID);return -1;}
  snprintf(changed.guide_url,sizeof(changed.guide_url),"%s",checked);
 }
 if(save_link(&changed)){*message=TR(IPTV_SAVE_FAILED);return -1;}
 *target=changed;
 if(!strcmp(current_list.url,target->url)){
  current_list=changed;current_list_saved=1;
  if(reset){
   snprintf(link,sizeof(link),"%s",current_list.url);
   snprintf(channel_agent,sizeof(channel_agent),"%s",current_list.user_agent);
   open_list(message);
  }
  else for(unsigned i=0;i<catalog.count;i++)snprintf(catalog.items[i].guide_url,sizeof(catalog.items[i].guide_url),"%s",changed.guide_url);
 }
 refresh_playlists();*message=TR(IPTV_SAVED);return 1;
}
static int options_for_page(UiContextAction items[16]) {
    int n=0,listing=catalog.count || saved_view;
    if(listing) {
        VtIptvChannel *item=selected_channel();
        if(item) {
            items[n++]=(UiContextAction){vt_iptv_channel_collection(item)?TR(IPTV_MODE_LIST):TR(IPTV_PLAY),NULL,18,1};
            items[n++]=(UiContextAction){item->starred?TR(UI_UNSTAR):channel_saved(item)?TR(UI_STAR):TR(IPTV_SAVE_FAVOURITE),NULL,10,1};
            if(!vt_iptv_channel_collection(item)&&!channel_saved(item))items[n++]=(UiContextAction){TR(IPTV_SAVE_CHANNEL),NULL,23,1};
            if(saved_view||picker_focus||(!vt_iptv_channel_collection(item)&&channel_saved(item))) {
                items[n++]=(UiContextAction){TR(IPTV_RENAME),NULL,14,1};
                items[n++]=(UiContextAction){TR(IPTV_DELETE),NULL,15,1};
            }
        }
        if(saved_view&&!picker_focus)items[n++]=(UiContextAction){starred_view?TR(UI_SHOW_ALL):TR(UI_SHOW_STARRED),NULL,11,1};
        else if(!picker_focus) {
            items[n++]=(UiContextAction){current_list.starred?TR(IPTV_UNSTAR_LIST):TR(IPTV_STAR_LIST),NULL,12,current_list.url[0]!=0};
            items[n++]=(UiContextAction){TR(IPTV_SAVE_LIST),NULL,16,current_list.url[0]!=0};
        }
        if(!saved_view||saved_channels)items[n++]=(UiContextAction){TR(IPTV_CHANNEL_ORDER),vt_preferences_iptv_name_sort()?TR(IPTV_ORDER_NAME):TR(IPTV_ORDER_SOURCE),26,1};
        VtIptvChannel *guide=guide_target();
        if(guide){
            items[n++]=(UiContextAction){TR(IPTV_GUIDE_URL),NULL,24,1};
            if(guide->guide_url[0])items[n++]=(UiContextAction){TR(IPTV_GUIDE_RESET),NULL,25,1};
        }
        items[n++]=(UiContextAction){TR(UI_REFRESH),NULL,13,1};
        if(saved_view){
            items[n++]=(UiContextAction){TR(IPTV_DIRECT_STREAM),NULL,8,1};
        }
        else {
            items[n++]=(UiContextAction){hide_unavailable?TR(IPTV_SHOW_UNAVAILABLE):TR(IPTV_CHECK_FILTER),NULL,17,catalog.count>0};
            items[n++]=(UiContextAction){TR(UI_BACK),NULL,5,1};
        }
    } else {
        items[n++]=(UiContextAction){TR(IPTV_EDIT),NULL,0,1};
        items[n++]=(UiContextAction){TR(IPTV_QR),NULL,3,1};
        items[n++]=(UiContextAction){mode==VT_IPTV_LIST?TR(IPTV_ADD_OPEN):TR(IPTV_SAVE_PLAY),NULL,7,link[0]!=0};
        items[n++]=(UiContextAction){TR(IPTV_COLLECTION),NULL,5,1};
        items[n++]=(UiContextAction){form_playlist?TR(IPTV_DIRECT_STREAM):TR(IPTV_ADD),NULL,8,1};
    }
    items[n++]=(UiContextAction){saved_view&&saved_channels?TR(IPTV_PLAYLISTS):TR(IPTV_SAVED_CHANNELS),NULL,20,1};
    items[n++]=(UiContextAction){split_view?TR(IPTV_SWITCH_SEPARATE):TR(IPTV_SWITCH_SPLIT),NULL,22,1};
    items[n++]=(UiContextAction){TR(IPTV_IMPORT_FILE),NULL,27,1};
    /* Playback first, then collection tools, presentation, and maintenance. */
    const int library[]={10,11,12,15,16,19,20,23};
    const int manage[]={0,3,8,13,14,24,25,27};
    for(unsigned i=0;i<sizeof(library)/sizeof(*library);i++)ui_context_menu_category(items,n,library[i],UI_MENU_GROUP_LIBRARY);
    for(unsigned i=0;i<sizeof(manage)/sizeof(*manage);i++)ui_context_menu_category(items,n,manage[i],UI_MENU_GROUP_MAINTENANCE);
    ui_context_menu_category(items,n,18,UI_MENU_GROUP_PLAYBACK);
    ui_context_menu_category(items,n,7,UI_MENU_GROUP_PLAYBACK);
    ui_context_menu_category(items,n,22,UI_MENU_GROUP_VIEW);
    ui_context_menu_category(items,n,26,UI_MENU_GROUP_VIEW);
    ui_context_menu_category(items,n,17,UI_MENU_GROUP_VIEW);
    ui_context_menu_category(items,n,5,UI_MENU_GROUP_LIBRARY);
    for(int i=1;i<n;i++){
        UiContextAction v=items[i];int j=i;
        int rank=v.enabled>>8;
        while(j&&((items[j-1].enabled>>8)>rank)){items[j]=items[j-1];j--;}
        items[j]=v;
    }
    return n;
}

int ui_iptv_screen(void) {
    const char *message = "";
    int focus = 0, action_focus=-1;
    split_view=vt_preferences_iptv_split();refresh_playlists();
    picker_focus=split_view&&((saved_view&&!saved_channels)||!catalog.count);picker_cursor=playlist_count?1:0;
    UiContextMenu options={0};
    UiFocusMotion focus_motion={0},grid_focus={0};
    UiScrollView scroll={0};uint64_t scroll_context=0,last_frame=0;
    if(!catalog.count) { saved_view=1; starred_view=0; catalog_focus=catalog_top=0;
        if(reload_saved())message=TR(IPTV_SAVE_FAILED); }
    if(!catalog.count&&!split_view)action_focus=4;
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
        uint64_t frame_time=sceKernelGetProcessTimeWide();
        float scroll_dt=last_frame&&frame_time>last_frame?(frame_time-last_frame)/1000000.f:1.f/60;
        if(scroll_dt>.05f)scroll_dt=.05f;
        last_frame=frame_time;
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
        UiContextAction option_items[16];int option_count=options_for_page(option_items);
        int virtual_channels_action=0;
        int option_action=ui_context_menu_input(&options,option_items,option_count,&pressed,controls.buttons,controls.ly,&touches,&touch);
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
        if(option_action==26){
            change_channel_sort(&message);pressed=touches=0;option_action=-1;
        }
        if(option_action==22){
            if(vt_preferences_set_iptv_split(!split_view))message=TR(IPTV_SAVE_FAILED);
            else {split_view=!split_view;picker_focus=split_view&&(saved_view&&!saved_channels);action_focus=-1;filter_dirty=1;refresh_playlists();ui_scroll_reset(&picker_scroll,0);}
            pressed=touches=0;option_action=-1;
        }
        int columns=channel_columns(),catalog_step=channel_step();
        int viewport_bottom=ui_mini_player_top()-IPTV_CATALOG_FOOTER_H;
        int viewport_height=viewport_bottom-IPTV_CATALOG_Y;
        catalog_visible=((viewport_height+catalog_step-1)/catalog_step+1)*columns;
        filter_catalog();
        uint64_t context=ui_page_key(saved_view?(saved_channels?"saved-channels":"playlists"):current_list.url,search)^((uint64_t)starred_view<<48)^((uint64_t)split_view<<49)^((uint64_t)vt_preferences_iptv_name_sort()<<50)^result_count;
        if(!scroll.ready||context!=scroll_context){ui_scroll_reset(&scroll,(catalog_top/columns)*catalog_step);scroll_context=context;grid_focus.initialized=0;}
        ui_scroll_bounds(&scroll,(float)((result_count+columns-1)/columns)*catalog_step-8,viewport_height);
        float old_scroll=scroll.position;
        int stick_scroll=page_input&&!picker_focus&&action_focus<0&&result_count&&abs((int)controls.ly-128)>32&&
            !(controls.ly<96&&scroll.position<=.1f&&catalog_focus<columns);
        if(stick_scroll&&ui_scroll_stick(&scroll,controls.ly,scroll_dt)){
            int first=(int)floorf(scroll.position/catalog_step);
            int last=(int)floorf((scroll.position+viewport_height-1)/catalog_step);
            clamp_channel_focus(first,last);
        }
        if(split_view&&page_input&&(catalog.count||saved_view)){
            if(!picker_focus&&action_focus>=0&&(pressed&SCE_CTRL_DOWN)&&
               ((saved_view&&!saved_channels)||action_focus==4)){
                picker_focus=1;action_focus=-1;pressed&=~SCE_CTRL_DOWN;
            }
            if((touches&(UI_TOUCH_EVENT_DOWN|UI_TOUCH_EVENT_TAP))&&touch.x>=18&&touch.x<328&&touch.y>=IPTV_CATALOG_Y&&touch.y<viewport_bottom){picker_focus=1;action_focus=-1;}
            if((touches&(UI_TOUCH_EVENT_DOWN|UI_TOUCH_EVENT_TAP))&&touch.x>=340&&touch.y>=IPTV_CATALOG_Y&&touch.y<viewport_bottom&&(!saved_view||saved_channels)){picker_focus=0;action_focus=-1;}
            if(!picker_focus&&action_focus<0&&catalog_focus%columns==0&&(pressed&SCE_CTRL_LEFT)){picker_focus=1;pressed&=~SCE_CTRL_LEFT;}
            if(picker_focus){
                unsigned nav=ui_nav_repeat_update(&repeat,pressed,controls.buttons,128,controls.ly,SCE_CTRL_UP|SCE_CTRL_DOWN);
                if((nav&SCE_CTRL_UP)&&picker_cursor>0)picker_cursor--;
                else if((nav&SCE_CTRL_UP)&&!picker_cursor){picker_focus=0;action_focus=4;}
                if((nav&SCE_CTRL_DOWN)&&picker_cursor<(int)playlist_count)picker_cursor++;
                ui_list_scroll_input(&picker_scroll,playlist_count+1,76,18,IPTV_CATALOG_Y,302,viewport_height,nav,controls.buttons,controls.ly,&touches,&touch,&picker_cursor);
                if((pressed&SCE_CTRL_RIGHT)&&(!saved_view||saved_channels)){picker_focus=0;action_focus=-1;}
                if((touches&UI_TOUCH_EVENT_TAP)&&touch.x>=18&&touch.x<320&&touch.y>=IPTV_CATALOG_Y&&touch.y<viewport_bottom){
                    int i=(int)((touch.y-IPTV_CATALOG_Y+picker_scroll.position)/76);
                    if(i>=0&&i<=(int)playlist_count){picker_cursor=i;pressed|=SCE_CTRL_CROSS;}touches=0;
                }
                if(picker_focus&&(pressed&SCE_CTRL_CROSS)){
                    if(!picker_cursor){virtual_channels_action=1;option_action=20;}
                    else option_action=18;
                    pressed&=~SCE_CTRL_CROSS;
                }
                pressed&=~(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
            }else if((touches&UI_TOUCH_EVENT_DOWN)&&touch.x>=340&&touch.y>=IPTV_CATALOG_Y){action_focus=-1;}
        }
        if(page_input && !picker_focus && (catalog.count || saved_view)) {
            if(!result_count&&action_focus<4)action_focus=search[0]?5:4;
            unsigned nav=ui_nav_repeat_update(&repeat,pressed,controls.buttons,controls.lx,stick_scroll?128:controls.ly,
                SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_UP|SCE_CTRL_DOWN);
            action_focus=iptv_grid_focus(action_focus,&catalog_focus,catalog_top,result_count,
                                            catalog_step,nav,!catalog.count&&!split_view,channel_list_x(),channel_list_width(),IPTV_CATALOG_Y,columns);
            if(nav&&action_focus<0){
                scroll.dragging=0;ui_scroll_reveal(&scroll,(catalog_focus/columns)*catalog_step,
                    (catalog_focus/columns)*catalog_step+catalog_step-8,viewport_height);
            }
            if(action_focus>=0&&(pressed&SCE_CTRL_CROSS)) {
                if(action_focus==4)pressed|=SCE_CTRL_START;
                else if(action_focus==5)pressed|=SCE_CTRL_TRIANGLE;
                else if(action_focus==7){
                    options.open=1;options.cursor=options.top=0;ui_nav_repeat_reset(&options.repeat);
                    page_input=0;
                }
                pressed&=~SCE_CTRL_CROSS;
            }
            pressed&=~(SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
            if(pressed&SCE_CTRL_SELECT){option_action=saved_view?14:16;pressed&=~SCE_CTRL_SELECT;}

        }
        if(option_action==20){
            list_parent_count=0;            iptv_cover_stop();saved_channels=virtual_channels_action?1:!(saved_view&&saved_channels);saved_view=1;search[0]=0;catalog_focus=catalog_top=0;action_focus=-1;
            message=reload_saved()?TR(IPTV_SAVE_FAILED):"";picker_focus=split_view&&!saved_channels;pressed=touches=0;option_action=-1;
        }
        if(split_view&&picker_focus&&(option_action==14||option_action==15)&&selected_channel()){
            VtIptvChannel *item=selected_channel();int is_current=!strcmp(current_list.url,item->url);
            if(option_action==14)rename_saved_link(item,&message);
            else if(remove_saved_link(item,&message)>0&&is_current){saved_view=1;saved_channels=0;reload_saved();}
            refresh_playlists();pressed=touches=0;option_action=-1;
            sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
        }
        if(option_action==5&&split_view&&!picker_focus&&(catalog.count||saved_view)){picker_focus=1;action_focus=-1;option_action=-1;pressed=touches=0;}
        int root_action=-1;
        if(option_action>=0 && option_action<10) {
            iptv_cover_stop();
            if(option_action==8){
                int direct=catalog.count||saved_view||form_playlist;
                new_link();if(direct){form_playlist=0;mode=VT_IPTV_HLS;}
                focus=0;
            }else if(option_action==0&&(catalog.count||saved_view)){
                new_link();focus=0;
            }else root_action=option_action;
            catalog.count=0;saved_view=0;action_focus=-1;
        }
        if(option_action==27){
            section=import_local_file(&message);if(section!=UI_SECTION_NONE&&section!=UI_SECTION_IPTV)return section;
            picker_focus=0;action_focus=-1;focus=2;pressed=touches=0;option_action=-1;page_input=0;
        }
        if(option_action==23&&selected_channel()){
            VtIptvChannel copy=*selected_channel();
            message=save_link(&copy)?TR(IPTV_SAVE_FAILED):TR(IPTV_LINK_SAVED);
            pressed=touches=0;option_action=-1;
        }
        if(option_action==10)pressed|=SCE_CTRL_SQUARE;
        if(option_action==18)pressed|=SCE_CTRL_CROSS;
        if(option_action==19)pressed|=SCE_CTRL_TRIANGLE;
        if(option_action==11 && saved_view&&!picker_focus){iptv_cover_stop();starred_view=!starred_view;reload_saved();catalog_focus=catalog_top=0;}
        if(option_action==12 && !saved_view && current_list.url[0]) {
            int old=current_list.starred;
            int result=toggle_star(&current_list);
            message=result?TR(IPTV_SAVE_FAILED):old?TR(ONLINE_REMOVED):TR(IPTV_SAVED);
            if(!result){current_list_saved=1;if(!strcmp(link,current_list.url))form_link_saved=1;}
        }
        if(option_action==24||option_action==25){
            iptv_cover_stop();edit_guide(option_action==25,&message);
            sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
            pressed=touches=0;page_input=0;
        }
        if(option_action==17) {
            availability_filter(&message);filter_catalog();memset(&focus_motion,0,sizeof(focus_motion));
            sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
            pressed=touches=0;page_input=0;
        }
        if(option_action==13) {
            iptv_cover_stop();
            if(picker_focus){refresh_playlists();}
            else if(saved_view) { reload_saved(); catalog_focus=catalog_top=0; }
            else {
                char previous_search[sizeof(search)];snprintf(previous_search,sizeof(previous_search),"%s",search);
                snprintf(link,sizeof(link),"%s",current_list.url);
                snprintf(channel_agent,sizeof(channel_agent),"%s",current_list.user_agent);
                open_list(&message);
                snprintf(search,sizeof(search),"%s",previous_search);filter_dirty=1;
            }
        }
        if(option_action==14 && selected_channel() && (saved_view||channel_saved(selected_channel()))) {
            iptv_cover_stop();
            rename_saved_link(selected_channel(),&message);
            sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
            pressed=touches=0;page_input=0;
        }
        if(option_action==15 && selected_channel() && (saved_view||channel_saved(selected_channel()))) {
            iptv_cover_stop();
            if(remove_saved_link(selected_channel(),&message)>0) {
                if(saved_view){if(reload_saved())message=TR(IPTV_SAVE_FAILED);}
                else {
                    selected_channel()->starred=0;memset(&selected_channel()->preferred,0,sizeof(selected_channel()->preferred));
                    refresh_playlists();filter_dirty=1;
                }
                if(catalog_focus>=(int)catalog.count)catalog_focus=catalog.count?catalog.count-1:0;
                if(catalog_top>catalog_focus)catalog_top=catalog_focus;
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
        if (page_input && ui_mini_player_handle_touch(touches, &touch)) touches = 0;
        filter_catalog();
        if (catalog.count || saved_view) {
            catalog_top=(int)floorf(scroll.position/catalog_step)*columns;
            iptv_cover_tick(); vt_video_thumbnail_resume(); vt_video_thumbnail_pump();
        }
        if (page_input && (catalog.count || saved_view)) {
            if(catalog.count&&(touches&UI_TOUCH_EVENT_TAP) && ui_touch_hit_rect(touch.x,touch.y,272,UI_TABS_Y,468,40)) {
                pressed|=SCE_CTRL_TRIANGLE;touches=0;
            }
            if(pressed&SCE_CTRL_TRIANGLE) {
                iptv_cover_stop();search_catalog();filter_catalog();message="";
                memset(&focus_motion,0,sizeof(focus_motion));
                sceCtrlPeekBufferPositive(0,&previous,1);ui_touch_reset();ui_nav_repeat_reset(&repeat);
                pressed=touches=0;
            }
            int in_view=!picker_focus&&touch.down_x>=channel_list_x()&&touch.down_y>=IPTV_CATALOG_Y&&touch.down_y<viewport_bottom;
            if((touches&UI_TOUCH_EVENT_DOWN)&&in_view)ui_scroll_drag_begin(&scroll,touch.y);
            if(!picker_focus&&scroll.dragging&&(touches&(UI_TOUCH_EVENT_MOVE|UI_TOUCH_EVENT_HOLD|UI_TOUCH_EVENT_UP))){
                ui_scroll_drag_move(&scroll,touch.down_y,touch.y,scroll_dt);
                if(touches&UI_TOUCH_EVENT_UP)ui_scroll_drag_end(&scroll,vt_preferences_reduce_motion());
                if(!(touches&UI_TOUCH_EVENT_TAP)){
                    int first=(int)floorf(scroll.position/catalog_step);
                    int last=(int)floorf((scroll.position+viewport_height-1)/catalog_step);
                    clamp_channel_focus(first,last);
                    action_focus=-1;
                }
            }
            if(!picker_focus&&(touches&UI_TOUCH_EVENT_TAP)&&touch.x>=channel_list_x()&&touch.y>=IPTV_CATALOG_Y&&touch.y<viewport_bottom){
                int row=(int)floorf((touch.y-IPTV_CATALOG_Y+scroll.position)/catalog_step);
                int card_width=channel_card_width(),col=(touch.x-channel_list_x())/(card_width+8);
                int index=row*columns+col;
                float y=IPTV_CATALOG_Y+row*catalog_step-scroll.position;
                if(col>=0&&col<columns&&index>=0&&index<(int)result_count&&ui_touch_hit_rect(touch.x,touch.y,channel_list_x()+col*(card_width+8),(int)y,card_width,catalog_step-8)){
                    catalog_focus=index;action_focus=-1;pressed|=SCE_CTRL_CROSS;
                }
            }
            if(pressed&SCE_CTRL_SQUARE && selected_channel()) {
                VtIptvChannel *item=selected_channel();char keep[VT_IPTV_URL_MAX];snprintf(keep,sizeof(keep),"%s",item->url);
                int was_starred=item->starred,old_focus=catalog_focus,old_top=catalog_top;
                iptv_cover_stop();message=toggle_star(item)?TR(IPTV_SAVE_FAILED):was_starred?TR(ONLINE_REMOVED):TR(IPTV_SAVED);
                if(saved_view){reload_saved();restore_saved_selection(keep,old_focus,old_top);}
            }
            if ((touches&UI_TOUCH_EVENT_TAP) && ui_touch_hit_rect(touch.x,touch.y,
                (catalog.count||split_view)?UI_CONTENT_X:68,(catalog.count||split_view)?UI_TABS_Y:306,(catalog.count||split_view)?240:824,(catalog.count||split_view)?40:52))pressed|=SCE_CTRL_START;
            if(pressed&SCE_CTRL_START) { iptv_cover_stop();new_link();focus=0;action_focus=-1;message="";memset(&focus_motion,0,sizeof(focus_motion)); }
            else if (pressed & SCE_CTRL_CIRCLE) {
                iptv_cover_stop();
                action_focus=-1;
                if(!saved_view&&list_parent_count){
                    current_list=list_parents[--list_parent_count];snprintf(link,sizeof(link),"%s",current_list.url);
                    snprintf(channel_agent,sizeof(channel_agent),"%s",current_list.user_agent);
                    open_list(&message);picker_focus=0;
                }else if(split_view){
                    if(!picker_focus)picker_focus=1;
                    else return UI_SECTION_LOCAL_MEDIA;
                }else if(saved_view&&saved_channels){
                    saved_channels=0;search[0]=0;catalog_focus=catalog_top=0;reload_saved();
                }else if(saved_view)return UI_SECTION_LOCAL_MEDIA;
                else if(list_from_form){
                    snprintf(link,sizeof(link),"%s",current_list.url);
                    snprintf(channel_title,sizeof(channel_title),"%s",current_list.title);
                    snprintf(channel_agent,sizeof(channel_agent),"%s",current_list.user_agent);
                    mode=VT_IPTV_LIST;form_playlist=1;form_link_saved=current_list_saved;
                    catalog.count=0;saved_view=0;search[0]=0;filter_dirty=1;focus=2;
                }else{
                    saved_view=1;saved_channels=0;snprintf(search,sizeof(search),"%s",saved_search);
                    reload_saved();restore_saved_selection(saved_parent_url,saved_parent_focus,saved_parent_top);
                }
            }
            else if ((pressed & SCE_CTRL_CROSS) && selected_channel()) {
                iptv_cover_stop(); VtIptvChannel *item=selected_channel();
                snprintf(link,sizeof(link),"%s",item->url); snprintf(channel_title,sizeof(channel_title),"%s",item->title);
                snprintf(channel_agent,sizeof(channel_agent),"%s",item->user_agent);
                mode=item->mode;
                if(mode==VT_IPTV_LIST){
                    VtIptvChannel opening=*item;if(!push_list_parent(opening.url,&message))continue;
                    if(saved_view){saved_parent_focus=catalog_focus;saved_parent_top=catalog_top;snprintf(saved_parent_url,sizeof(saved_parent_url),"%s",item->url);}
                    list_from_form=0;current_list=opening;action_focus=-1;
                    if(open_list(&message)){
                        /* A broken/cancelled playlist returns to the browser,
                         * never strands the user in the link-entry form. */
                        saved_view=1;saved_channels=0;reload_saved();picker_focus=split_view;
                    }else {picker_focus=0;if(current_list.mode!=VT_IPTV_LIST){section=play_link(&message);if(section!=UI_SECTION_NONE&&section!=UI_SECTION_IPTV)return section;}}
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
            if(focus==1&&!form_playlist&&(nav&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT)))
                mode=(mode+(nav&SCE_CTRL_LEFT?3:1))%4;
            else focus=iptv_form_focus(focus,nav,form_playlist,!!link[0]);
            int action=root_action>=0?root_action:(pressed&SCE_CTRL_CROSS)?form_actions[focus]:-1;
            if(pressed&SCE_CTRL_START)action=0;
            else if(pressed&SCE_CTRL_TRIANGLE)action=3;
            else if(pressed&SCE_CTRL_SQUARE)action=4;
            else if((pressed&SCE_CTRL_SELECT)&&!form_playlist)action=6;
            else if(pressed&SCE_CTRL_CIRCLE)action=5;
            if(touches&UI_TOUCH_EVENT_TAP)for(int i=0;i<IPTV_FORM_COUNT;i++)
                if(!(i==1&&form_playlist)&&!(i==7&&!form_playlist))
                if(ui_touch_hit_rect(touch.x,touch.y,form_rects[i][0],form_rects[i][1],form_rects[i][2],form_rects[i][3])) {focus=i;action=form_actions[i];}
            if(action==27||(action==0&&vt_iptv_local_path(link))){
                section=import_local_file(&message);if(section!=UI_SECTION_NONE&&section!=UI_SECTION_IPTV)return section;
                focus=2;action_focus=-1;picker_focus=0;
            }else if (action == 0) {
                char input[VT_IPTV_URL_MAX];
                if (ui_text_input(TR(IPTV_LINK), link, input, sizeof(input)) > 0) {
                    if (!accept_link(input)) {
                        if(form_playlist)mode=VT_IPTV_LIST;
                        focus = 2; message = mode==VT_IPTV_LIST?TR(IPTV_LINK_READY):"";
                    } else message = TR(IPTV_INVALID);
                }
            } else if (action == 1 && !link[0]) {
                focus=0;message=TR(IPTV_EMPTY);
            } else if (action == 1) {
                if(mode==VT_IPTV_LIST) {
                    memset(&current_list,0,sizeof(current_list));snprintf(current_list.url,sizeof(current_list.url),"%s",link);
                    snprintf(current_list.title,sizeof(current_list.title),"%s",channel_title[0]?channel_title:TR(IPTV_MODE_LIST));current_list.mode=VT_IPTV_LIST;
                    list_from_form=1;int opened=open_list(&message);action_focus=-1;section=UI_SECTION_NONE;
                    if(!opened&&current_list.mode!=VT_IPTV_LIST)section=play_link(&message);
                } else section = play_link(&message);
                if (section != UI_SECTION_NONE && section != UI_SECTION_IPTV) return section;
            } else if(action==3) {
                char scanned[VT_IPTV_URL_MAX];
                int scan=ui_qr_scan_https_url(scanned,sizeof(scanned));
                if(scan>0){if(!accept_link(scanned)){if(form_playlist)mode=VT_IPTV_LIST;focus=2;message=mode==VT_IPTV_LIST?TR(IPTV_LINK_READY):"";}else message=TR(IPTV_INVALID);}
                else if(scan<0)message=ui_qr_scan_last_error();
            } else if((action==4||action==7) && !link[0]) {
                focus=0;message=TR(IPTV_EMPTY);
            } else if(action==4||action==7) {
                VtIptvChannel item={0};char checked[VT_IPTV_URL_MAX];
                if(vt_iptv_resource_resolve(link,link,checked,sizeof(checked)))message=TR(IPTV_INVALID);
                else {
                    snprintf(item.url,sizeof(item.url),"%s",link);item.mode=mode;
                    snprintf(item.user_agent,sizeof(item.user_agent),"%s",channel_agent);
                    snprintf(item.title,sizeof(item.title),"%s",channel_title);
                    if(action==4)rename_saved_link(&item,&message);
                    else {
                        default_link_name(&item);
                        snprintf(channel_title,sizeof(channel_title),"%s",item.title);
                        if(mode==VT_IPTV_LIST){
                            current_list=item;list_from_form=1;
                            if(!open_list(&message)){
                                if(save_link(&current_list)){
                                    current_list_saved=form_link_saved=0;catalog.count=0;filter_dirty=1;
                                    focus=2;message=TR(IPTV_SAVE_FAILED);
                                }
                                else {
                                    current_list_saved=form_link_saved=1;list_from_form=0;
                                    saved_search[0]=0;saved_parent_focus=saved_parent_top=0;
                                    snprintf(saved_parent_url,sizeof(saved_parent_url),"%s",current_list.url);
                                    refresh_playlists();picker_focus=0;action_focus=-1;message=current_list.mode==VT_IPTV_LIST?TR(IPTV_LIST_ADDED):TR(IPTV_SAVED);
                                    if(current_list.mode!=VT_IPTV_LIST){section=play_link(&message);if(section!=UI_SECTION_NONE&&section!=UI_SECTION_IPTV)return section;}
                                }
                            }
                        }else if(save_link(&item))message=TR(IPTV_SAVE_FAILED);
                        else {
                            form_link_saved=1;section=play_link(&message);
                            if(section!=UI_SECTION_NONE&&section!=UI_SECTION_IPTV)return section;
                        }
                    }
                }
            } else if(action==5) {
                iptv_cover_stop();saved_view=1;saved_channels=0;picker_focus=split_view;action_focus=-1;catalog_focus=catalog_top=0;
                snprintf(search,sizeof(search),"%s",saved_search);
                message=reload_saved()?TR(IPTV_SAVE_FAILED):"";
                restore_saved_selection(saved_parent_url,saved_parent_focus,saved_parent_top);
            } else if(action==6&&!form_playlist) mode=(mode+1)%4;
            else if(action==9){options.open=1;options.cursor=options.top=0;ui_nav_repeat_reset(&options.repeat);}
            if (action >= 0) {
                sceCtrlPeekBufferPositive(0, &previous, 1);
                ui_touch_reset(); ui_nav_repeat_reset(&repeat);
            }
        } else ui_nav_repeat_reset(&repeat);
        filter_catalog();
        columns=channel_columns();catalog_step=channel_step();
        uint64_t final_context=ui_page_key(saved_view?(saved_channels?"saved-channels":"playlists"):current_list.url,search)^((uint64_t)starred_view<<48)^((uint64_t)split_view<<49)^((uint64_t)vt_preferences_iptv_name_sort()<<50)^result_count;
        if(final_context!=scroll_context){ui_scroll_reset(&scroll,(catalog_top/columns)*catalog_step);scroll_context=final_context;grid_focus.initialized=0;}
        ui_scroll_bounds(&scroll,(float)((result_count+columns-1)/columns)*catalog_step-8,viewport_height);
        if(!page_input)scroll.dragging=scroll.coasting=0;
        ui_scroll_tick(&scroll,frame_time,vt_preferences_reduce_motion());
        if(scroll.coasting&&result_count){
            int first=(int)floorf(scroll.position/catalog_step);
            int end=(int)ceilf((scroll.position+viewport_height)/catalog_step);
            clamp_channel_focus(first,end-1);
        }
        catalog_top=(int)floorf(scroll.position/catalog_step)*columns;
        if(grid_focus.initialized)grid_focus.y+=old_scroll-scroll.position;
        filter_catalog();
        char shown[VT_IPTV_URL_MAX]; display_link(shown);
        vita2d_start_drawing(); vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG, VT_THEME_SIGNAL_BRIGHT);
        if(catalog.count||saved_view)ui_menu_surface(0,54,960,490,1);
        ui_brand_draw_breadcrumbs(TR(IPTV_TITLE), NULL,
            saved_view ? (saved_channels?TR(IPTV_SAVED_CHANNELS):TR(IPTV_PLAYLISTS)) : catalog.count ? current_list.title : form_playlist?TR(IPTV_ADD):TR(IPTV_DIRECT_STREAM));
        ui_action_button(756,UI_TABS_Y,186,40,VT_THEME_SURFACE,UI_PAD_R1,TR(UI_OPTIONS),options.open);
        if(catalog.count || saved_view) {
            int empty=!catalog.count&&!split_view;
            ui_action_button(empty?68:UI_CONTENT_X,empty?306:UI_TABS_Y,empty?824:240,empty?52:40,
                VT_THEME_SURFACE_RAISED,empty?"":"",TR(IPTV_ADD),0);
            if(action_focus==4)ui_focus_motion_tick(&focus_motion,empty?68:UI_CONTENT_X,empty?306:UI_TABS_Y,empty?824:240,empty?52:40);
            if(!empty){
            char count_label[192];snprintf(count_label,sizeof(count_label),"%s  %u/%u",search[0]?search:TR(IPTV_SEARCH_SHORT),result_count,catalog.count);
            ui_action_button(272,UI_TABS_Y,468,40,VT_THEME_SURFACE,UI_PAD_TRIANGLE,count_label,0);
            if(action_focus==5)ui_focus_motion_tick(&focus_motion,272,UI_TABS_Y,468,40);
            }
            vita2d_set_clip_rectangle(0,IPTV_CATALOG_Y,960,viewport_bottom);vita2d_enable_clipping();
            int first_row=(int)floorf(scroll.position/catalog_step);
            int final_row=(int)ceilf((scroll.position+viewport_height)/catalog_step);
            for(int index=first_row*columns;index<final_row*columns&&index<(int)result_count;index++) {
                int card_width=channel_card_width();
                int x=channel_list_x()+(index%columns)*(card_width+8),y=(int)lroundf(IPTV_CATALOG_Y+(index/columns)*catalog_step-scroll.position);VtIptvChannel *item=&catalog.items[results[index]];
                int selected=page_input && !picker_focus && action_focus<0 && index==catalog_focus;
                ui_menu_row(x,y,card_width,catalog_step-8,0,1);
                if(selected)ui_focus_motion_tick(&grid_focus,x,y,card_width,catalog_step-8);
                vita2d_texture *cover=item->artwork_path[0] && strcmp(item->artwork_path,"-") ? vt_artwork_thumbnail_get(item->artwork_path) : NULL;
                int art_size=columns>1?48:catalog_step-24; if(art_size>92)art_size=92;
                ui_artwork_tile(x+8,y+(catalog_step-8-art_size)/2,art_size,cover,vt_iptv_channel_collection(item)?"M3U":"TV");
                char title[192];snprintf(title,sizeof(title),"%s%s",item->starred?"* ":"",item->title);
                ui_menu_text(x+72,y+27,card_width-88,title,UI_MENU_TEXT,UI_FONT_BODY);
                char detail[192];snprintf(detail,sizeof(detail),"%s",item->group[0]?item->group:channel_type_label(item));
                if(item->preferred.height){size_t used=strlen(detail);snprintf(detail+used,sizeof(detail)-used," · %up",item->preferred.height);}
                ui_menu_text(x+72,y+52,card_width-88,detail,UI_MENU_MUTED,UI_FONT_SMALL);
                if(item->mode!=VT_IPTV_LIST&&!saved_view&&!item->starred&&channel_saved(item))ui_menu_text(x+72,y+74,card_width-88,TR(IPTV_LINK_SAVED),UI_MENU_MUTED,UI_FONT_SMALL);
            }
            if(page_input&&!picker_focus&&action_focus<0&&result_count)ui_menu_focus_draw(&grid_focus);
            vita2d_disable_clipping();
            if(action_focus==7)ui_focus_motion_tick(&focus_motion,756,UI_TABS_Y,186,40);
            if(page_input && action_focus>=0)ui_focus_outline_draw(&focus_motion);
            if(!result_count&&!split_view) {
                text_line(68,empty?180:240,820,search[0]?TR(IPTV_NO_RESULTS):saved_channels?TR(IPTV_NO_CHANNELS):TR(IPTV_NO_PLAYLISTS),VT_THEME_TEXT,UI_FONT_BODY);
                ui_text_block(68,empty?224:274,820,26,3,search[0]?TR(IPTV_CLEAR_SEARCH):saved_channels?TR(IPTV_SAVED_EXPLANATION):TR(IPTV_NO_SAVED_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
            }
            if(split_view){
                ui_menu_text(28,IPTV_CATALOG_Y-14,282,TR(IPTV_PLAYLISTS),UI_MENU_ACCENT,UI_FONT_SMALL);
                char pane_title[96];snprintf(pane_title,sizeof(pane_title),"%s (%u)",saved_view&&saved_channels?TR(IPTV_SAVED_CHANNELS):TR(IPTV_CHANNELS),result_count);
                ui_menu_text(350,IPTV_CATALOG_Y-14,582,pane_title,UI_MENU_ACCENT,UI_FONT_SMALL);
                vita2d_draw_rectangle(329,IPTV_CATALOG_Y-38,1,viewport_bottom-IPTV_CATALOG_Y+38,ui_theme_alpha(UI_MENU_ACCENT,110));
                vita2d_set_clip_rectangle(18,IPTV_CATALOG_Y,320,viewport_bottom);vita2d_enable_clipping();
                ui_scroll_bounds(&picker_scroll,(playlist_count+1)*76,viewport_height);
                ui_scroll_tick(&picker_scroll,frame_time,vt_preferences_reduce_motion());
                if(picker_focus&&page_input){ui_focus_motion_tick(&picker_motion,18,IPTV_CATALOG_Y+picker_cursor*76-picker_scroll.position,302,68);ui_menu_focus_draw(&picker_motion);}
                for(int i=ui_list_scroll_first(&picker_scroll,76);i<ui_list_scroll_end(&picker_scroll,76,viewport_height,playlist_count+1);i++){
                    float y=IPTV_CATALOG_Y+i*76-picker_scroll.position;
                    ui_menu_row(18,y,302,68,0,1);
                    ui_menu_text(30,y+28,244,i?playlist_entries[i-1].title:TR(IPTV_SAVED_CHANNELS),UI_MENU_TEXT,UI_FONT_BODY);
                    if((!i&&saved_view&&saved_channels)||(i&&!saved_view&&!strcmp(playlist_entries[i-1].url,current_list.url)))ui_menu_text(286,y+28,24,"✓",UI_MENU_ACCENT,UI_FONT_SMALL);
                    ui_menu_text(30,y+52,270,i?channel_type_label(&playlist_entries[i-1]):TR(IPTV_CHANNELS),UI_MENU_MUTED,UI_FONT_SMALL);
                }
                vita2d_disable_clipping();
                ui_scroll_rail(322,IPTV_CATALOG_Y,viewport_height,picker_scroll.position,viewport_height,picker_scroll.maximum+viewport_height);
                if(!result_count)ui_menu_text(354,IPTV_CATALOG_Y+54,570,saved_view&&!saved_channels?TR(IPTV_PICK_PLAYLIST):TR(IPTV_NO_CHANNELS),UI_MENU_MUTED,UI_FONT_SMALL);
            }
            ui_scroll_rail(948,IPTV_CATALOG_Y,viewport_height,(int)scroll.position,viewport_height,(int)(scroll.maximum+viewport_height));
            if(message[0]||result_count||split_view)text_line(UI_CONTENT_X,ui_mini_player_top()-12,UI_CONTENT_W,message[0]?message:split_view?TR(IPTV_PANES_HINT):TR(IPTV_NAVIGATION_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        } else {
        text_line(18,89,720,form_playlist?TR(IPTV_LIST_INTRO):TR(IPTV_FORMATS),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        draw_link_field(shown);
        ui_action_button(18,218,596,52,link[0]?VT_THEME_SURFACE_RAISED:ui_theme_alpha(VT_THEME_SURFACE,96),"",mode==VT_IPTV_LIST?TR(IPTV_ADD_OPEN):TR(IPTV_SAVE_PLAY),0);
        ui_action_button(630,218,312,52,link[0]?VT_THEME_SURFACE:ui_theme_alpha(VT_THEME_SURFACE,96),"",TR(IPTV_OPEN_ONCE),0);
        ui_action_button(18,286,458,52,VT_THEME_SURFACE,UI_PAD_TRIANGLE,TR(IPTV_QR),0);
        ui_action_button(484,286,458,52,VT_THEME_SURFACE,UI_PAD_CIRCLE,TR(IPTV_COLLECTION),0);
        if(!form_playlist)ui_action_button(18,352,924,48,VT_THEME_SURFACE,"Select",mode_label(mode),0);
        else ui_action_button(18,352,924,48,VT_THEME_SURFACE,"",TR(IPTV_IMPORT_FILE),0);
        if(page_input) {
            if(focus!=6)ui_focus_motion_tick(&focus_motion,form_rects[focus][0],form_rects[focus][1],form_rects[focus][2],form_rects[focus][3]);
            else ui_focus_motion_tick(&focus_motion,756,UI_TABS_Y,186,40);
            ui_focus_outline_draw(&focus_motion);
        }
        ui_text_block(18,form_playlist?424:430,924,24,2,message[0]?message:form_playlist?TR(IPTV_ADD_HINT):TR(IPTV_DIRECT_HINT),VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
        }
        ui_mini_player_draw();
        option_count=options_for_page(option_items);
        ui_context_menu_draw(&options,TR(UI_OPTIONS),option_items,option_count);
        if (sidebar.animation > 0.01f) ui_sections_sidebar_draw(&sidebar);
        vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
        ui_frame_pace(frame_time);
    }
}

int ui_iptv_open_library(const VtLibraryItem *v) {
    snprintf(link, sizeof(link), "%s", v->locator);
    snprintf(channel_title, sizeof(channel_title), "%s", v->title);
    snprintf(channel_agent, sizeof(channel_agent), "%s", v->user_agent);
    mode = v->mode;
    memset(&current_list, 0, sizeof(current_list));
    char parent[VT_IPTV_URL_MAX];
    if(v->context[0]&&!vt_iptv_resource_resolve(v->context,v->context,parent,sizeof(parent))){
        snprintf(current_list.url,sizeof(current_list.url),"%s",parent);current_list.mode=VT_IPTV_LIST;
        VtIptvCatalog *saved=calloc(1,sizeof(*saved));
        if(saved&&!vt_iptv_saved_load(saved,0))for(unsigned i=0;i<saved->count;i++)if(!strcmp(saved->items[i].url,parent)){current_list=saved->items[i];break;}
        free(saved);
    }
    const char *message = NULL;library_parent_active=current_list.url[0]!=0;
    int ret = play_link(&message);library_parent_active=0;
    return ret;
}
