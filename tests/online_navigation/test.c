#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#define SCE_CTRL_SELECT 2048
#include "../../src/ui/brand.h"
/* Exercise the actual screen transactions, including its private state. */
#include "../../src/ui/online_audio_screen.c"

static unsigned checks, messages;
static int request_result, request_cancel, request_count;
static int player_action, player_error, stop_count;
static VtOnlineQuery before;
#define CHECK(x) do { checks++; assert(x); } while (0)
void log_printf(const char *format, ...) { (void)format; }
int log_save(const char *path) { CHECK(!strcmp(path, VITAMEDIADECK_SESSION_LOG_PATH)); return 0; }
const char *vt_i18n_str(VtStringId id) { (void)id; return "test"; }
int sceKernelWaitThreadEnd(SceUID id, void *a, void *b) { (void)id; (void)a; (void)b; return 0; }
int sceKernelDeleteThread(SceUID id) { (void)id; return 0; }
void ui_message_show(const char *a, const char *b, int duration) {
    (void)a; (void)b; (void)duration; messages++;
}
int vt_online_load_saved(VtOnlineItem *items, int capacity) {
    CHECK(capacity > 0); memset(items, 0, sizeof(*items));
    items->kind = VT_ONLINE_FEED; strcpy(items->title, "Favorite"); return 1;
}
int vt_online_fetch_query(void *ctx) {
    VtOnlineQuery *q = ctx; q->count = request_count; q->cancel = request_cancel;
    strcpy(q->items[0].title, "Replacement"); q->items[0].kind = VT_ONLINE_EPISODE;
    return request_result;
}
int ui_loading_run(const char *message, UiLoadingTaskFn task, void *ctx,
    volatile int *cancel, const volatile long *current, const volatile long *total) {
    (void)message; (void)cancel; (void)current; (void)total; return task(ctx);
}
int vt_background_playback_snapshot(VtBackgroundPlaybackSnapshot *s) {
    memset(s, 0, sizeof(*s)); s->state = player_error ? VT_BACKGROUND_ERROR : VT_BACKGROUND_PLAYING;
    return 1;
}
void vt_background_playback_stop(void) { stop_count++; }
int ui_music_player_run(const char *a, const char *b, const char *c, uint32_t bitrate) {
    (void)a; (void)b; (void)c; (void)bitrate; return player_action;
}
static void reset(void) {
    memset(&g_query, 0, sizeof(g_query));
    g_query.kind = VT_ONLINE_FEED; g_query.count = 8;
    strcpy(g_query.query, "Original"); strcpy(g_query.items[5].title, "Favorite");
    g_selected = 5; g_top = 3; g_home = 1; g_tab = VT_ONLINE_FEED;
    g_episodes = g_menu = g_catalog = g_return_valid = 0;
    g_pages = 1; g_page_sizes[0] = 8; g_lazy_thread = g_cover_thread = -1;
    request_result = request_cancel = 0; request_count = 2; messages = 0;
    before = g_query;
}
int main(void) {
    reset(); request_result = -1;
    CHECK(fetch_page(VT_ONLINE_EPISODE, "https://example.org/feed", 0, 1) < 0);
    CHECK(!memcmp(&before, &g_query, sizeof(before)));
    CHECK(!g_episodes && !g_catalog && !g_return_valid && !g_menu);
    CHECK(g_selected == 5 && g_top == 3 && messages == 1);

    reset(); request_cancel = 1;
    CHECK(fetch_page(VT_ONLINE_EPISODE, "https://example.org/feed", 0, 1) < 0);
    CHECK(!memcmp(&before, &g_query, sizeof(before)));
    CHECK(!g_episodes && !g_return_valid && messages == 0);

    reset(); request_count = 0;
    CHECK(fetch_page(VT_ONLINE_EPISODE, "https://example.org/feed", 0, 1) < 0);
    CHECK(!memcmp(&before, &g_query, sizeof(before)) && !g_episodes);

    reset();
    CHECK(fetch_page(VT_ONLINE_EPISODE, "https://example.org/feed", 0, 1) == 0);
    CHECK(g_episodes && g_return_valid && !g_catalog && !g_menu && g_query.count == 2);
    CHECK(!memcmp(&before, &g_return_query, sizeof(before)));
    playback_failed();
    CHECK(!memcmp(&before, &g_query, sizeof(before)) && !g_episodes && !g_return_valid);
    CHECK(g_selected == 5 && g_top == 3 && g_page_sizes[0] == 8);

    /* Returning from the global Player entry uses the same failure recovery. */
    CHECK(fetch_page(VT_ONLINE_EPISODE, "https://example.org/feed", 0, 1) == 0);
    player_error = 1; player_action = UI_MUSIC_PLAYER_STOP;
    CHECK(radio_fullscreen(0, NULL) < 0 && stop_count == 1);
    CHECK(!g_episodes && g_selected == 5 && g_top == 3);

    CHECK(fetch_page(VT_ONLINE_EPISODE, "https://example.org/feed", 0, 1) == 0);
    player_error = 0; player_action = UI_MUSIC_PLAYER_MINIMIZE;
    CHECK(radio_fullscreen(0, NULL) == 0 && stop_count == 1 && g_episodes);
    g_home = 0; playback_failed(); CHECK(g_episodes); /* Discovery keeps its context. */
    restore_list(); CHECK(!g_episodes && g_selected == 5);

    reset(); g_menu = 1; g_query.kind = VT_ONLINE_MENU; before = g_query;
    request_result = -1;
    CHECK(fetch_page(VT_ONLINE_RADIO, "", 0, 0) < 0);
    CHECK(g_menu && !memcmp(&before, &g_query, sizeof(before)));

    reset(); g_episodes = 1; playback_failed();
    CHECK(!g_episodes && g_query.count == 1 && g_query.kind == VT_ONLINE_FEED);
    printf("Online navigation: %u assertions passed\n", checks);
    return 0;
}
