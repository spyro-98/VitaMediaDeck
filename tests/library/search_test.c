/* Production fan-out and cache, with independently delayed provider fixtures. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
int sceKernelDelayThread(unsigned);
#include "../../src/library/search.c"
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        __atomic_add_fetch(&checks, 1, __ATOMIC_RELAXED);                                          \
        assert(x);                                                                                 \
    } while (0)
static pthread_t pool[3];
static SceKernelThreadEntry entries[3];
static int created;
static int remember = 1;
int vita_https_is_connected(void) {
    return 1;
}
static pthread_mutex_t fixture_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixture_cond = PTHREAD_COND_INITIALIZER;
static int release_slow, slow_waiting, source_count = 3;
static VtNetworkSource sources[3];
int sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority,
                          unsigned stack, unsigned flags, int cpu, void *opt) {
    (void)name;
    (void)flags;
    (void)cpu;
    (void)opt;
    CHECK(priority == 0x10000140 && stack == 0x40000);
    entries[created] = entry;
    return ++created;
}
static void *pool_main(void *ctx) {
    int i = (int)(intptr_t)ctx;
    entries[i](sizeof(i), &i);
    return NULL;
}
int sceKernelStartThread(SceUID id, SceSize n, void *ctx) {
    (void)n;
    (void)ctx;
    return pthread_create(&pool[id - 1], NULL, pool_main, (void *)(intptr_t)(id - 1));
}
int sceKernelWaitThreadEnd(SceUID id, int *a, void *b) {
    (void)a;
    (void)b;
    return pthread_join(pool[id - 1], NULL);
}
int sceKernelDeleteThread(SceUID id) {
    (void)id;
    return 0;
}
int sceKernelDelayThread(unsigned us) {
    (void)us;
    usleep(1000);
    return 0;
}
uint64_t sceKernelGetProcessTimeWide(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000ULL + t.tv_nsec / 1000;
}
int vt_preferences_remember_network_passwords(void) {
    return remember;
}
int vt_network_sources_load(VtNetworkSource *out, int cap) {
    CHECK(cap >= 3);
    memcpy(out, sources, sizeof(sources));
    return source_count;
}
int vt_network_credentials_load(VtNetworkCredential *out, int cap) {
    CHECK(cap >= 3);
    for (int i = 0; i < 3; i++)
        if (i != 2)
            strcpy(out[i].password, "fixture-secret");
    return 3;
}
int ui_network_personal_resume_credential(const VtNetworkSource *s, VtNetworkCredential *c) {
    (void)s;
    (void)c;
    return 0;
}
void vt_network_media_history_id(const VtNetworkSource *s, const char *path, char out[16]) {
    uint64_t hash = 14695981039346656037ULL;
    const char *parts[] = {s->host, s->username, path};
    for (int i = 0; i < 3; i++)
        for (const unsigned char *p = (const unsigned char *)parts[i]; *p; p++)
            hash = (hash ^ *p) * 1099511628211ULL;
    snprintf(out, 16, "r%014llx", (unsigned long long)(hash & 0xffffffffffffffULL));
}
void vt_playback_history_local_id(const char *p, char out[16]) {
    uint32_t hash = 2166136261U;
    while (*p)
        hash = (hash ^ (unsigned char)*p++) * 16777619U;
    snprintf(out, 16, "media%08x", hash);
}
uint64_t vt_playback_history_order(const char *s) {
    (void)s;
    return 0;
}
int vt_playback_history_progress(const char *s, uint64_t *p, uint64_t *d) {
    (void)s;
    (void)p;
    (void)d;
    return 0;
}
int ui_local_media_search(const char *q, UiLocalMediaVisitor visit, void *ctx) {
    for (int i = 0; i < 300; i++) {
        VtLocalMediaItem v = {.type = VT_LOCAL_MEDIA_AUDIO};
        snprintf(v.path, sizeof(v.path), "ux0:music/%s-%03d.mp3", q, i);
        snprintf(v.name, sizeof(v.name), "%s local %03d", q, i);
        if (!visit(&v, ctx))
            break;
    }
    return 0;
}
int vt_online_load_saved(VtOnlineItem *v, int cap) {
    (void)v;
    (void)cap;
    return 0;
}
int vt_iptv_saved_load(VtIptvCatalog *v, int stars) {
    (void)stars;
    memset(v, 0, sizeof(*v));
    return 0;
}
int vt_online_fetch_query(void *ctx) {
    VtOnlineQuery *q = ctx;
    usleep(1000);
    q->count = 1;
    VtOnlineItem *v = q->items;
    v->kind = q->kind == VT_ONLINE_RADIO     ? VT_ONLINE_RADIO
              : q->kind == VT_ONLINE_EPISODE ? VT_ONLINE_EPISODE
                                             : VT_ONLINE_FEED;
    snprintf(v->title, sizeof(v->title), "%s %s", q->query,
             q->kind == VT_ONLINE_RADIO ? "radio" : "podcast");
    snprintf(v->url, sizeof(v->url), "https://online.example/%s/%d", q->query, v->kind);
    strcpy(v->id, "episode-guid");
    return 0;
}
int vt_network_prepare_source(const VtNetworkSource *s, VtNetworkCredential *c, char *detail,
                              size_t n) {
    (void)detail;
    (void)n;
    CHECK(!strcmp(c->password, "fixture-secret"));
    if (s->protocol == VT_NETWORK_JELLYFIN) {
        pthread_mutex_lock(&fixture_mutex);
        slow_waiting++;
        pthread_cond_broadcast(&fixture_cond);
        while (!release_slow)
            pthread_cond_wait(&fixture_cond, &fixture_mutex);
        pthread_mutex_unlock(&fixture_mutex);
    }
    return 0;
}
int vt_network_list(const VtNetworkSource *s, const VtNetworkCredential *c, const char *path,
                    VtNetworkEntry *out, int cap, char *detail, size_t n) {
    (void)c;
    (void)detail;
    (void)n;
    CHECK(cap >= 100);
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s %s", path + 7,
             s->protocol == VT_NETWORK_JELLYFIN ? "slow server" : "music server");
    snprintf(out->path, sizeof(out->path), "song:%s", path + 7);
    out->is_audio = s->protocol == VT_NETWORK_NAVIDROME;
    out->is_video = !out->is_audio;
    return 1;
}
int vt_iptv_http_fetch(void *a, const char *b, size_t c, unsigned char **d, size_t *e, char *f,
                       size_t g, volatile int *h) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    return -1;
}
int vt_iptv_catalog_parse(const void *a, size_t b, const char *c, VtIptvCatalog *d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return -1;
}
static int has_provider(VtLibraryResult *v, int n, int p) {
    for (int i = 0; i < n; i++)
        if (v[i].item.provider == p)
            return 1;
    return 0;
}
static int wait_results(VtLibraryResult *out, VtLibrarySearchStatus *status, int complete) {
    for (int tick = 0; tick < 2000; tick++) {
        int n = vt_library_search_snapshot(out, VT_LIBRARY_RESULTS, status);
        if (complete ? !status->pending
                     : (has_provider(out, n, VT_LIBRARY_NAVIDROME) &&
                        has_provider(out, n, VT_LIBRARY_RADIO) &&
                        has_provider(out, n, VT_LIBRARY_PODCAST)))
            return n;
        usleep(1000);
    }
    CHECK(!"search timed out");
    return 0;
}
int main(void) {
    sources[0].protocol = VT_NETWORK_JELLYFIN;
    strcpy(sources[0].host, "https://slow.example");
    sources[1].protocol = VT_NETWORK_NAVIDROME;
    strcpy(sources[1].host, "https://music.example");
    strcpy(sources[1].username, "account-a");
    sources[2].protocol = VT_NETWORK_IMMICH;
    strcpy(sources[2].host, "https://photos.example");
    CHECK(!vt_library_search_init());
    VtLibraryResult *out = calloc(VT_LIBRARY_RESULTS, sizeof(*out));
    CHECK(out);
    VtLibrarySearchStatus state;
    vt_library_search_begin("oldslow", NULL);
    int n = wait_results(out, &state, 0);
    CHECK(state.pending > 0);
    CHECK(has_provider(out, n, VT_LIBRARY_LOCAL));
    CHECK(has_provider(out, n, VT_LIBRARY_RADIO));
    CHECK(has_provider(out, n, VT_LIBRARY_PODCAST));
    CHECK(state.state[VT_LIBRARY_IMMICH] == VT_SEARCH_LOGIN ||
          state.state[VT_LIBRARY_IMMICH] == VT_SEARCH_LOADING);
    /* Replacing a query does not join the blocked source or accept its old reply. */
    uint64_t begin = sceKernelGetProcessTimeWide();
    vt_library_search_begin("newquery", NULL);
    CHECK(sceKernelGetProcessTimeWide() - begin < 100000);
    n = wait_results(out, &state, 0);
    CHECK(state.pending > 0);
    pthread_mutex_lock(&fixture_mutex);
    release_slow = 1;
    pthread_cond_broadcast(&fixture_cond);
    pthread_mutex_unlock(&fixture_mutex);
    n = wait_results(out, &state, 1);
    CHECK(has_provider(out, n, VT_LIBRARY_JELLYFIN) &&
          (has_provider(out, n, VT_LIBRARY_NAVIDROME) && has_provider(out, n, VT_LIBRARY_RADIO) &&
           has_provider(out, n, VT_LIBRARY_PODCAST)));
    CHECK(state.state[VT_LIBRARY_IMMICH] == VT_SEARCH_LOGIN);
    for (int i = 0; i < n; i++) {
        CHECK(!strstr(out[i].item.title, "oldslow"));
        CHECK(!strstr(out[i].item.locator, "fixture-secret"));
        if (i)
            CHECK(out[i - 1].item.provider <= out[i].item.provider);
    }
    n = vt_library_query("oldslow", VT_LIBRARY_ALL, out, VT_LIBRARY_RESULTS);
    CHECK(!has_provider(out, n, VT_LIBRARY_JELLYFIN));
    /* A completed query is available immediately from persistent-cache metadata. */
    vt_library_search_begin("newquery", NULL);
    n = vt_library_search_snapshot(out, VT_LIBRARY_RESULTS, &state);
    CHECK((has_provider(out, n, VT_LIBRARY_NAVIDROME) && has_provider(out, n, VT_LIBRARY_RADIO) &&
           has_provider(out, n, VT_LIBRARY_PODCAST)));
    /* RAM-only sessions work with remembered passwords disabled and are account scoped. */
    remember = 0;
    VtNetworkCredential credential = {0};
    strcpy(credential.password, "fixture-secret");
    vt_library_search_set_session(&sources[1], &credential);
    credential = (VtNetworkCredential){0};
    CHECK(vt_library_search_get_session(&sources[1], &credential) && credential.password[0]);
    VtNetworkSource another = sources[1];
    strcpy(another.username, "account-b");
    credential = (VtNetworkCredential){0};
    CHECK(!vt_library_search_get_session(&another, &credential) && !credential.password[0]);
    vt_library_search_begin("session", NULL);
    n = wait_results(out, &state, 1);
    CHECK((has_provider(out, n, VT_LIBRARY_NAVIDROME) && has_provider(out, n, VT_LIBRARY_RADIO) &&
           has_provider(out, n, VT_LIBRARY_PODCAST)));
    CHECK(state.state[VT_LIBRARY_JELLYFIN] == VT_SEARCH_READY);
    strcpy(sources[0].username, "different-account");
    vt_library_search_begin("isolated", NULL);
    n = wait_results(out, &state, 1);
    CHECK(state.state[VT_LIBRARY_JELLYFIN] == VT_SEARCH_LOGIN);
    vt_library_search_cancel();
    vt_library_search_shutdown();
    free(out);
    printf("Global search: %u checks; actual worker pool, blocked server, progressive/local/cache "
           "results, query replacement, grouping, cache pressure, auth and account isolation "
           "passed.\n",
           checks);
    return 0;
}

int vt_personal_authenticate_cancel(const VtNetworkSource *s, VtNetworkCredential *c, char *d,
                                    size_t n, volatile int *cancel) {
    (void)cancel;
    return vt_network_prepare_source(s, c, d, n);
}
int vt_jellyfin_authenticate_cancel(const VtNetworkSource *s, VtNetworkCredential *c, char *d,
                                    size_t n, volatile int *cancel) {
    (void)cancel;
    return vt_network_prepare_source(s, c, d, n);
}
int vt_personal_list_cancel(const VtNetworkSource *s, const VtNetworkCredential *c, const char *p,
                            VtNetworkEntry *e, int cap, char *d, size_t n, volatile int *cancel) {
    (void)cancel;
    return vt_network_list(s, c, p, e, cap, d, n);
}
int vt_jellyfin_list_cancel(const VtNetworkSource *s, const VtNetworkCredential *c, const char *p,
                            VtNetworkEntry *e, int cap, char *d, size_t n, volatile int *cancel) {
    (void)cancel;
    return vt_network_list(s, c, p, e, cap, d, n);
}

int vt_online_fetch_cover(VtOnlineItem *v, volatile int *c) {
    (void)v;
    (void)c;
    return -1;
}
