/* Real catalog/checkpoint implementation with POSIX filesystem and threads. */
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define SCE_O_APPEND O_APPEND
#include "../../src/library/library.c"
static unsigned checks, writes, bytes, syncs, cycles, fail_sync, fail_write, short_write,
    io_on_main;
static uint64_t virtual_time;
static pthread_t saver_thread, main_thread;
static SceKernelThreadEntry saver_entry;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        __atomic_add_fetch(&checks, 1, __ATOMIC_RELAXED);                                          \
        assert(x);                                                                                 \
    } while (0)
static unsigned get(unsigned *p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static void set(unsigned *p, unsigned n) {
    __atomic_store_n(p, n, __ATOMIC_RELEASE);
}
static void delay(void) {
    usleep(1000);
}
uint64_t sceKernelGetProcessTimeWide(void) {
    return __atomic_load_n(&virtual_time, __ATOMIC_ACQUIRE);
}
int sceIoOpen(const char *p, int flags, int mode) {
    return open(p, flags, mode);
}
int sceIoRead(int f, void *p, unsigned n) {
    return (int)read(f, p, n);
}
int sceIoWrite(int f, const void *p, unsigned n) {
    if (pthread_equal(pthread_self(), main_thread))
        set(&io_on_main, 1);
    if (get(&fail_write))
        return -1;
    if (get(&short_write) && n > 7)
        n = 7;
    int ret = (int)write(f, p, n);
    if (ret > 0) {
        __atomic_add_fetch(&writes, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&bytes, (unsigned)ret, __ATOMIC_RELAXED);
    }
    return ret;
}
int sceIoClose(int f) {
    return close(f);
}
int sceIoSyncByFd(int f, int flags) {
    (void)flags;
    if (get(&fail_sync))
        return -1;
    __atomic_add_fetch(&syncs, 1, __ATOMIC_RELAXED);
    return fsync(f);
}
int sceIoMkdir(const char *p, int mode) {
    return mkdir(p, mode);
}
int sceIoRemove(const char *p) {
    return unlink(p);
}
int sceIoRename(const char *a, const char *b) {
    return rename(a, b);
}
int sceIoGetstat(const char *p, SceIoStat *s) {
    struct stat st;
    int ret = stat(p, &st);
    if (!ret)
        s->st_size = st.st_size;
    return ret;
}
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry fn, int priority,
                             unsigned stack, unsigned flags, int cpu, void *opt) {
    (void)name;
    (void)flags;
    (void)cpu;
    (void)opt;
    CHECK(priority >= 176 && priority <= 191 && stack == 0x10000);
    saver_entry = fn;
    return 1;
}
static void *saver_main(void *p) {
    (void)p;
    saver_entry(0, NULL);
    return NULL;
}
int sceKernelStartThread(SceUID id, SceSize args, void *p) {
    (void)args;
    (void)p;
    CHECK(id == 1);
    return pthread_create(&saver_thread, NULL, saver_main, NULL);
}
int sceKernelWaitThreadEnd(SceUID id, int *status, void *timeout) {
    (void)status;
    (void)timeout;
    CHECK(id == 1);
    return pthread_join(saver_thread, NULL);
}
int sceKernelDeleteThread(SceUID id) {
    CHECK(id == 1);
    return 0;
}
int sceKernelDelayThreadCB(unsigned us) {
    (void)us;
    __atomic_add_fetch(&cycles, 1, __ATOMIC_RELAXED);
    delay();
    return 0;
}
int sceKernelCreateCallback(const char *s, unsigned attr, SceKernelCallbackFunction f, void *p) {
    (void)s;
    (void)attr;
    (void)f;
    (void)p;
    return 3;
}
int sceKernelDeleteCallback(SceUID id) {
    CHECK(id == 3);
    return 0;
}
int scePowerRegisterCallback(int id) {
    CHECK(id == 3);
    return 0;
}
int scePowerUnregisterCallback(int id) {
    CHECK(id == 3);
    return 0;
}
static void drain(void) {
    vt_library_checkpoint();
    for (int wait = 0; wait < 3000; wait++) {
        pthread_mutex_lock(&mutex);
        int dirty = 0;
        for (int i = 0; i < count; i++)
            dirty |= slots[i].metadata_revision || slots[i].state_revision;
        pthread_mutex_unlock(&mutex);
        if (!dirty)
            return;
        delay();
    }
    CHECK(!"checkpoint timed out");
}
static VtLibraryItem make_item(int provider, const char *path, const char *name) {
    VtLibraryItem v = {.provider = provider, .kind = VT_LIBRARY_VIDEO, .duration_ms = 3600000};
    snprintf(v.locator, sizeof(v.locator), "%s", path);
    snprintf(v.title, sizeof(v.title), "%s", name);
    snprintf(v.history_id, sizeof(v.history_id), "h%d_%zu", provider, strlen(path));
    vt_library_identity(&v);
    return v;
}
static void restart(void) {
    vt_library_shutdown();
    CHECK(!vt_library_init(NULL));
}
static VtLibraryItem racing;
static void *mutator(void *ctx) {
    (void)ctx;
    VtLibraryItem v = racing;
    for (int i = 0; i < 1000; i++) {
        snprintf(v.artist, sizeof(v.artist), "Artist %d", i);
        vt_library_put(&v);
        vt_library_progress(v.history_id, (unsigned)i * 1000, 3600000);
    }
    return NULL;
}
int main(void) {
    char path[] = "/tmp/vmd-library-core.XXXXXX";
    CHECK(mkdtemp(path) && !chdir(path));
    CHECK(!mkdir("ux0:data", 0700));
    main_thread = pthread_self();
    CHECK(!vt_library_init(NULL));
    VtLibraryResult *out = calloc(VT_LIBRARY_MAX, sizeof(*out));
    CHECK(out);
    VtLibraryItem a = make_item(VT_LIBRARY_LOCAL, "ux0:video/one.mp4", "Blade Runner"),
                  b = make_item(VT_LIBRARY_NAVIDROME, "song:two", "Night City");
    strcpy(a.artist, "Vangelis");
    strcpy(a.album, "Soundtrack");
    CHECK(vt_library_matches(&a, "blade VANGELIS soundtrack"));
    CHECK(!vt_library_matches(&a, "missing"));
    CHECK(vt_library_matches(&a, "   "));
    CHECK(vt_library_matches(&a, "one.mp4 VANGELIS"));
    CHECK(!vt_library_matches(&b, "song:two"));
    CHECK(!vt_library_matches(&a, "ux0:video"));
    vt_library_put(&a);
    CHECK(vt_library_get(a.id, out) && !strcmp(out->item.title, a.title));
    CHECK(!vt_library_get("missing", out));
    CHECK(vt_library_resume_fallback(&a, 90000) == 90000);
    CHECK(!vt_library_query("", VT_LIBRARY_RECENT, out, 512));
    vt_library_import(&a, 60000, 3600000, 10);
    CHECK(vt_library_resume(&a) == 60000);
    vt_library_started(&a);
    vt_library_progress(a.history_id, 120000, 0);
    drain();
    CHECK(vt_library_resume(&a) == 120000);
    vt_library_started(&b);
    vt_library_progress(b.history_id, 30000, 3600000);
    CHECK(vt_library_query("", VT_LIBRARY_CONTINUE, out, 512) == 2);
    CHECK(!strcmp(out[0].item.id, b.id));
    restart();
    CHECK(vt_library_resume(&a) == 120000 && vt_library_resume(&b) == 30000);
    /* A real rewind to zero defeats older history, even after restarting. */
    vt_library_progress(a.history_id, 0, 3600000);
    drain();
    restart();
    CHECK(!vt_library_resume_fallback(&a, 120000));
    vt_library_import(&a, 200000, 3600000, 99);
    CHECK(!vt_library_resume(&a));
    vt_library_progress(b.history_id, 3590000, 3600000);
    CHECK(!vt_library_query("", VT_LIBRARY_CONTINUE, out, 512));
    CHECK(vt_library_query("", VT_LIBRARY_RECENT, out, 512) == 2);
    VtLibraryItem live = make_item(VT_LIBRARY_RADIO, "https://station.example/live", "Station");
    live.kind = VT_LIBRARY_LIVE;
    vt_library_identity(&live);
    vt_library_started(&live);
    vt_library_progress(live.id, 90000, 3600000);
    CHECK(!vt_library_resume(&live));
    CHECK(vt_library_query("", VT_LIBRARY_RECENT, out, 512) == 3);
    /* Canonical Jellyfin ids ignore navigation ancestry; server/account differ. */
    VtLibraryItem remote = make_item(VT_LIBRARY_JELLYFIN, "folder/episode-id", "Episode");
    strcpy(remote.server, "serverA");
    vt_library_identity(&remote);
    VtLibraryItem same = remote;
    strcpy(same.locator, "episode-id");
    vt_library_identity(&same);
    CHECK(!strcmp(remote.id, same.id));
    strcpy(same.server, "serverB");
    vt_library_identity(&same);
    CHECK(strcmp(remote.id, same.id));
    /* Metadata refresh preserves a played item's alias, cover and parent context. */
    strcpy(remote.artwork, "ux0:cover.jpg");
    strcpy(remote.context, "series");
    vt_library_started(&remote);
    vt_library_progress(remote.history_id, 60000, 0);
    same = remote;
    same.artwork[0] = same.context[0] = 0;
    strcpy(same.history_id, "new_alias");
    vt_library_put(&same);
    CHECK(vt_library_resume(&same) == 60000);
    CHECK(vt_library_query("Episode", VT_LIBRARY_ALL, out, 512) == 1 &&
          !strcmp(out[0].item.artwork, "ux0:cover.jpg") && !strcmp(out[0].item.context, "series"));
    vt_library_progress(remote.history_id, 61000, 0);
    CHECK(vt_library_resume(&same) == 61000);
    drain();
    /* Failed writes and syncs remain dirty, retry without UI I/O, then recover. */
    set(&fail_write, 1);
    vt_library_progress(a.history_id, 150000, 3600000);
    vt_library_checkpoint();
    unsigned before = get(&cycles);
    while (get(&cycles) < before + 4)
        delay();
    set(&fail_write, 0);
    set(&short_write, 1);
    drain();
    set(&short_write, 0);
    restart();
    CHECK(vt_library_resume(&a) == 150000);
    set(&fail_sync, 1);
    vt_library_progress(a.history_id, 160000, 3600000);
    vt_library_checkpoint();
    before = get(&cycles);
    while (get(&cycles) < before + 4)
        delay();
    set(&fail_sync, 0);
    drain();
    restart();
    CHECK(vt_library_resume(&a) == 160000);
    /* Torn tail keeps earlier CRC-valid frames; next save atomically repairs it. */
    vt_library_shutdown();
    FILE *f = fopen(DB, "ab");
    CHECK(f);
    CHECK(fwrite("torn", 1, 4, f) == 4);
    CHECK(!fclose(f));
    CHECK(!vt_library_init(NULL));
    CHECK(vt_library_resume(&a) == 160000);
    vt_library_progress(a.history_id, 170000, 0);
    drain();
    restart();
    CHECK(vt_library_resume(&a) == 170000);
    /* Real concurrent reads, metadata refresh, playback updates and saver writes. */
    racing = a;
    pthread_t editor;
    CHECK(!pthread_create(&editor, NULL, mutator, NULL));
    for (int i = 0; i < 500; i++) {
        vt_library_query("", VT_LIBRARY_RECENT, out, 512);
        vt_library_checkpoint();
    }
    CHECK(!pthread_join(editor, NULL));
    drain();
    VtLibraryItem clip = make_item(VT_LIBRARY_LOCAL, "ux0:short.mp4", "Short clip");
    clip.duration_ms = 12000;
    vt_library_started(&clip);
    vt_library_progress(clip.history_id, 12000, 12000);
    CHECK(!vt_library_resume(&clip));
    same = remote;
    strcpy(same.history_id, "canonical_alias");
    vt_library_bind(&same);
    vt_library_progress(same.history_id, 90000, 3600000);
    CHECK(vt_library_resume(&same) == 90000);
    /* Filling cache must retain played history and evict different old cache slots. */
    for (int i = 0; i < 700; i++) {
        char locator[64];
        snprintf(locator, sizeof(locator), "photo:%d", i);
        VtLibraryItem v = make_item(VT_LIBRARY_IMMICH, locator, locator);
        vt_library_put(&v);
    }
    CHECK(vt_library_query("photo:", VT_LIBRARY_ALL, out, 512) > 400);
    CHECK(vt_library_query("Episode", VT_LIBRARY_RECENT, out, 512) == 1);
    drain();
    /* One virtual hour: only small state frames after metadata has settled. */
    unsigned bytes_before = get(&bytes), sync_before = get(&syncs);
    for (unsigned second = 1; second <= 3600; second++) {
        vt_library_progress(a.history_id, second * 1000ULL, 7200000);
        __atomic_store_n(&virtual_time, second * 1000000ULL, __ATOMIC_RELEASE);
        unsigned tick = get(&cycles);
        while (get(&cycles) == tick)
            delay();
    }
    drain();
    unsigned payload = get(&bytes) - bytes_before, commits = get(&syncs) - sync_before;
    CHECK(commits <= 242);
    CHECK(payload < 20000);
    CHECK(!get(&io_on_main));
    restart();
    CHECK(vt_library_resume(&a) == 3600000);
    vt_library_shutdown();
    free(out);
    printf("Unified library: %u checks; virtual hour %u commits / %u journal bytes; restart, zero "
           "rewind, completion, identity, cache pressure, torn writes and real concurrent I/O "
           "ownership passed. %s\n",
           checks, commits, payload, path);
    return 0;
}
