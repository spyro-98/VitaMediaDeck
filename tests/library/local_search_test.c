/* Production immutable-index reader, actual records and POSIX filesystem. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <psp2/io/fcntl.h>
#include "library/catalog.h"
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        __atomic_add_fetch(&checks, 1, __ATOMIC_RELAXED);                                          \
        assert(x);                                                                                 \
    } while (0)
static pthread_t ui_thread;
static volatile int cancelled;
static int reads, yields, visits, cancel_at, stop_at, result;
static const char *query;
int sceKernelDelayThread(unsigned us) {
    CHECK(!pthread_equal(pthread_self(), ui_thread));
    CHECK(us == 1000);
    yields++;
    return 0;
}
int sceIoOpen(const char *path, int flags, int mode) {
    CHECK(!pthread_equal(pthread_self(), ui_thread));
    return open(path, flags, mode);
}
int sceIoRead(int fd, void *data, unsigned n) {
    CHECK(!pthread_equal(pthread_self(), ui_thread));
    if (n >= sizeof(VtLocalMediaItem)) {
        reads++;
        CHECK(n <= 8 * sizeof(VtLocalMediaItem));
        if (cancel_at && reads == cancel_at)
            __atomic_store_n(&cancelled, 1, __ATOMIC_RELEASE);
    }
    return (int)read(fd, data, n);
}
SceOff sceIoLseek(int fd, SceOff offset, int whence) {
    return lseek(fd, offset, whence);
}
int sceIoClose(int fd) {
    return close(fd);
}
void vt_playback_history_local_id(const char *path, char out[16]) {
    (void)path;
    strcpy(out, "media-fixture");
}
#include "local-search.inc"
static int visit(const VtLocalMediaItem *item, void *ctx) {
    (void)ctx;
    CHECK(!strcmp(item->artist, "Vangelis"));
    visits++;
    return !stop_at || visits < stop_at;
}
static void *run(void *ctx) {
    (void)ctx;
    result = ui_local_media_search(query, visit, NULL, &cancelled);
    return NULL;
}
static void search(const char *q, int cancel_batch, int stop, int preset_cancel) {
    query = q;
    reads = yields = visits = 0;
    cancel_at = cancel_batch;
    stop_at = stop;
    __atomic_store_n(&cancelled, preset_cancel, __ATOMIC_RELEASE);
    pthread_t t;
    CHECK(!pthread_create(&t, NULL, run, NULL));
    CHECK(!pthread_join(t, NULL));
}
int main(void) {
    ui_thread = pthread_self();
    char dir[] = "/tmp/vmd-local-search.XXXXXX";
    CHECK(mkdtemp(dir) && !chdir(dir));
    LocalMediaIndexHeader h = {.version = 8,
                               .record_size = sizeof(VtLocalMediaItem),
                               .total_count = 256,
                               .audio_count = 256};
    memcpy(h.magic, "VTLOCAL6", 8);
    FILE *f = fopen("local.idx", "wb");
    CHECK(f);
    CHECK(fwrite(&h, sizeof(h), 1, f) == 1);
    for (int i = 0; i < 256; i++) {
        VtLocalMediaItem item = {.type = VT_LOCAL_MEDIA_AUDIO};
        snprintf(item.path, sizeof(item.path), "ux0:music/physical-%03d.mp3", i);
        strcpy(item.name, "Display title");
        strcpy(item.artist, "Vangelis");
        strcpy(item.album, "OST");
        CHECK(fwrite(&item, sizeof(item), 1, f) == 1);
    }
    CHECK(!fclose(f));
    search("vangelis ost", 0, 0, 0);
    CHECK(!result && visits == 256 && reads == 32 && yields == 8);
    search("physical-009.mp3", 0, 1, 0);
    CHECK(!result && visits == 1 && reads == 2);
    search("does-not-exist", 2, 0, 0);
    CHECK(!result && !visits && reads == 2 && !yields);
    search("Display", 0, 0, 1);
    CHECK(!result && !visits && !reads);
    f = fopen("local.idx", "r+b");
    CHECK(f);
    h.version = 99;
    CHECK(fwrite(&h, sizeof(h), 1, f) == 1 && !fclose(f));
    search("Display", 0, 0, 0);
    CHECK(result < 0 && !visits && !reads);
    printf("Local metadata search: %u checks; actual index reader, filename/title/artist/album, "
           "8-record I/O, yield budget, zero-match cancellation and corrupt index passed.\n",
           checks);
    return 0;
}
