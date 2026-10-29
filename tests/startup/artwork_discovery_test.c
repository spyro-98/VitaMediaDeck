/* Slow removable storage must not lock every cached logo or cover. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int blocked, resume_scan;
static char slow_path[192];
static int storage_stat(const char *path, struct stat *st) {
    if (!strcmp(path, slow_path)) {
        pthread_mutex_lock(&gate); blocked = 1; pthread_cond_broadcast(&changed);
        while (!resume_scan) pthread_cond_wait(&changed, &gate);
        pthread_mutex_unlock(&gate);
    }
    return stat(path, st);
}
#define stat(path, st) storage_stat(path, st)
#include "../../src/network/artwork_cache.c"
#undef stat
static unsigned requests;
static char *fetch(const char *url, volatile int *cancel, size_t *size) {
    (void)url; (void)cancel; requests++; *size = 64;
    char *bytes = calloc(1, *size); assert(bytes); memcpy(bytes, "\x89PNG\r\n\x1a\n", 8); return bytes;
}
static void *discover(void *unused) { (void)unused; vt_artwork_cache_init(); return NULL; }
int main(void) {
    alarm(10);
    char dir[] = "/private/tmp/vmd-art-discovery.XXXXXX";
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("ux0:data", 0700) && !mkdir("ux0:data/VitaMediaDeck", 0700));
    char cached[192], restored[192], key[65]; volatile int cancel = 0;
    const char *url = "https://example.org/logo?private-data";
    assert(!vt_artwork_cache_get(url, cached, sizeof(cached), &cancel, fetch) && requests == 1);
    assert(!url_key("https://example.org/slow", key));
    snprintf(slow_path, sizeof(slow_path), ROOT "/%s.png", key);
    FILE *f = fopen(slow_path, "wb"); assert(f); assert(fwrite("\x89PNG\r\n\x1a\n", 1, 8, f) == 8); fclose(f);
    pthread_t scan; assert(!pthread_create(&scan, NULL, discover, NULL));
    pthread_mutex_lock(&gate); while (!blocked) pthread_cond_wait(&changed, &gate); pthread_mutex_unlock(&gate);
    /* Simulate a cold key while directory discovery is paused inside stat(). */
    pthread_mutex_lock(&mutex); memset(entries, 0, sizeof(entries)); pthread_mutex_unlock(&mutex);
    assert(!vt_artwork_cache_get(url, restored, sizeof(restored), &cancel, fetch));
    assert(!strcmp(cached, restored) && requests == 1);
    assert(!vt_artwork_cache_peek(url, restored, sizeof(restored)));
    pthread_mutex_lock(&gate); resume_scan = 1; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&gate);
    assert(!pthread_join(scan, NULL));
    /* A corrupt/traversing pointer cannot read any file outside the cache. */
    assert(!url_key("https://example.org/invalid-ref", key));
    char ref[192]; snprintf(ref, sizeof(ref), ROOT "/%s.ref", key);
    f = fopen(ref, "wb"); assert(f); fputs("../../private.png\n", f); fclose(f);
    assert(!vt_artwork_cache_get("https://example.org/invalid-ref", restored, sizeof(restored), &cancel, fetch));
    assert(requests == 2 && strstr(restored, ROOT "/") == restored);
    puts("Artwork startup: keyed cold-cache hit and render peek succeed while discovery I/O is blocked; no refetch, no global scan lock and unsafe reference rejection passed.");
}
