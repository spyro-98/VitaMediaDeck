#include "network/artwork_cache.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>
#ifdef __vita__
#include <mbedtls/sha256.h>
#else
#include <openssl/sha.h>
#endif

#define ROOT "ux0:data/VitaMediaDeck/artwork"
#define ITEM_LIMIT (2u * 1024u * 1024u)
#define CACHE_LIMIT (64u * 1024u * 1024u)
#define CACHE_ITEMS 128
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static const char *extensions[] = {"jpg", "png", "webp", "gif", "bmp"};
static int encoding(const unsigned char *p, size_t n) {
    if (n >= 3 && p[0] == 255 && p[1] == 216 && p[2] == 255) return 0;
    if (n >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8)) return 1;
    if (n >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WEBP", 4)) return 2;
    if (n >= 6 && (!memcmp(p, "GIF87a", 6) || !memcmp(p, "GIF89a", 6))) return 3;
    return n >= 2 && !memcmp(p, "BM", 2) ? 4 : -1;
}
static int lookup(const char *key, char *path, size_t capacity) {
    for (unsigned i = 0; i < sizeof(extensions)/sizeof(*extensions); i++) {
        snprintf(path, capacity, ROOT "/%s.%s", key, extensions[i]);
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
        unsigned char head[12]; size_t n = 0;
        FILE *f = fopen(path, "rb"); if (f) { n = fread(head, 1, sizeof(head), f); fclose(f); }
        if (st.st_size > 0 && st.st_size <= ITEM_LIMIT && encoding(head, n) == (int)i) {
            utime(path, NULL); return 0;
        }
        remove(path); /* Only a validated digest path inside our cache. */
    }
    path[0] = 0; return -1;
}
/* Evict oldest access first, not hash collisions. Bound both bytes and files. */
static int make_room(size_t incoming) {
    for (;;) {
        DIR *dir = opendir(ROOT); if (!dir) return -1;
        struct dirent *entry; size_t bytes = 0; unsigned count = 0;
        time_t oldest = 0; char victim[192] = "";
        while ((entry = readdir(dir))) {
            if (strlen(entry->d_name) < 68 || strspn(entry->d_name, "0123456789abcdef") != 64 || entry->d_name[64] != '.') continue;
            int known = 0;
            for (unsigned i = 0; i < sizeof(extensions)/sizeof(*extensions); i++)
                if (!strcmp(entry->d_name + 65, extensions[i])) known = 1;
            if (!known) continue;
            char path[192]; struct stat st;
            snprintf(path, sizeof(path), ROOT "/%.69s", entry->d_name);
            if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
            count++; bytes += st.st_size;
            if (!victim[0] || st.st_mtime < oldest) { oldest = st.st_mtime; snprintf(victim, sizeof(victim), "%s", path); }
        }
        closedir(dir);
        if (count < CACHE_ITEMS && bytes + incoming <= CACHE_LIMIT) return 0;
        if (!victim[0] || remove(victim)) return -1;
    }
}
int vt_artwork_cache_get(const char *url, char *path, size_t capacity,
                        volatile int *cancel, VtArtworkFetch fetch) {
    if (!url || !*url || !path || capacity < 160 || !fetch) return -1;
    path[0] = 0;
    if (cancel && *cancel) return -1;
    unsigned char digest[32]; char key[65];
#ifdef __vita__
    if (mbedtls_sha256((const unsigned char *)url, strlen(url), digest, 0)) return -1;
#else
    SHA256((const unsigned char *)url, strlen(url), digest);
#endif
    for (int i = 0; i < 32; i++) snprintf(key + 2 * i, 3, "%02x", digest[i]);
    pthread_mutex_lock(&mutex);
    int hit = lookup(key, path, capacity);
    pthread_mutex_unlock(&mutex);
    if (!hit) return 0;
    size_t size = 0; char *data = fetch(url, cancel, &size);
    int format = data ? encoding((const unsigned char *)data, size) : -1;
    if (format < 0 || !size || size > ITEM_LIMIT || (cancel && *cancel)) { free(data); return -1; }
    pthread_mutex_lock(&mutex);
    /* Recheck after the network wait in case another worker published it. */
    int result = lookup(key, path, capacity);
    if (result) {
        mkdir(ROOT, 0777);
        if (!make_room(size)) {
            char temp[192]; snprintf(temp, sizeof(temp), ROOT "/pending.tmp");
            snprintf(path, capacity, ROOT "/%s.%s", key, extensions[format]);
            FILE *f = fopen(temp, "wb"); int ok = 0;
            if (f) { ok = fwrite(data, 1, size, f) == size; if (fflush(f)) ok = 0; if (fclose(f)) ok = 0; }
            if (cancel && *cancel) ok = 0;
            if (ok && !rename(temp, path)) result = 0;
            else { remove(temp); path[0] = 0; }
        }
    }
    pthread_mutex_unlock(&mutex); free(data); return result;
}
