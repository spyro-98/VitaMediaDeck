#include "network/http_audio_stream.h"
#include "network/online_audio.h"
#include "common/text_log.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vita_https.h>
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#endif

/* Only the producer owns the HTTPS cursor. The audio-output thread reads
 * bounded compressed memory, never a synchronous range GET. */
#define BLOCK (512u * 1024u)
#define SLOTS 4
typedef struct { int64_t start; size_t size; unsigned char *data; } AudioBlock;
typedef struct {
    VitaHttpsClient *client;
    VitaHttpsStream stream;
    pthread_t worker;
    pthread_mutex_t mutex;
    const char *url;
    volatile int *parent_cancel;
    int cancelled, opened, error;
    int64_t position, length;
    AudioBlock blocks[SLOTS];
    unsigned char *storage;
    unsigned hits, waits, fetches;
} HttpAudio;
static uint64_t milliseconds(void) {
#ifdef __vita__
    return sceKernelGetProcessTimeWide() / 1000;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
#endif
}
static void tick(void) { usleep(5000); }
static int cancelled(HttpAudio *h) {
    if (h->parent_cancel && __atomic_load_n(h->parent_cancel, __ATOMIC_ACQUIRE))
        __atomic_store_n(&h->cancelled, 1, __ATOMIC_RELEASE);
    return __atomic_load_n(&h->cancelled, __ATOMIC_ACQUIRE);
}
static void audio_abort(void *ctx) {
    HttpAudio *h = ctx;
    __atomic_store_n(&h->cancelled, 1, __ATOMIC_RELEASE);
}
static void *prefetch(void *ctx) {
    HttpAudio *h = ctx;
    int result = vita_https_open_range_stream(h->client, h->url,
                                             &h->cancelled, &h->stream);
    pthread_mutex_lock(&h->mutex);
    h->length = h->stream.size;
    h->error = result < 0 ? result : h->length < 0 ? -1 : 0;
    h->opened = 1;
    pthread_mutex_unlock(&h->mutex);
    unsigned char *incoming = h->storage + SLOTS * BLOCK;
    while (!result && !cancelled(h)) {
        pthread_mutex_lock(&h->mutex);
        int64_t base = h->position / BLOCK * BLOCK, target = -1;
        for (int n = 0; n < SLOTS; n++) {
            int64_t next = base + (int64_t)n * BLOCK;
            if (next >= h->length) break;
            int found = 0;
            for (int i = 0; i < SLOTS; i++)
                if (h->blocks[i].start == next && h->blocks[i].size) found = 1;
            if (!found) { target = next; break; }
        }
        pthread_mutex_unlock(&h->mutex);
        if (target < 0) { tick(); continue; }
        uint64_t started = milliseconds();
        size_t wanted = h->length - target < BLOCK ? (size_t)(h->length - target) : BLOCK;
        int bytes = h->stream.seek(h->stream.opaque, target, SEEK_SET) == target
            ? h->stream.read(h->stream.opaque, incoming, wanted) : -1;
        pthread_mutex_lock(&h->mutex);
        h->fetches++;
        if (bytes != (int)wanted) { h->error = bytes < 0 ? bytes : -1; result = h->error; }
        else {
            /* A seek can overtake a GET. Discard obsolete bytes without
             * replacing the new demand's block or changing its position. */
            base = h->position / BLOCK * BLOCK;
            if (target >= base && target < base + (int64_t)SLOTS * BLOCK) {
                for (int i = 0; i < SLOTS; i++) {
                    AudioBlock *b = &h->blocks[i];
                    if (!b->size || b->start < base || b->start >= base + (int64_t)SLOTS * BLOCK) {
                        memcpy(b->data, incoming, bytes); b->start = target; b->size = bytes; break;
                    }
                }
            }
        }
        pthread_mutex_unlock(&h->mutex);
        if (milliseconds() - started >= 250)
            log_printf("HTTP read-ahead: offset=%lld bytes=%d fetch_ms=%llu\n",
                (long long)target, bytes, (unsigned long long)(milliseconds() - started));
    }
    return NULL;
}
static int audio_read(void *ctx, void *data, size_t size) {
    HttpAudio *h = ctx;
    if (!data) return -1;
    if (!size) return 0;
    uint64_t start = milliseconds(); int waited = 0;
    while (!cancelled(h)) {
        pthread_mutex_lock(&h->mutex);
        if (h->position >= h->length) { pthread_mutex_unlock(&h->mutex); return 0; }
        for (int i = 0; i < SLOTS; i++) {
            AudioBlock *b = &h->blocks[i];
            if (b->size && h->position >= b->start && h->position < b->start + (int64_t)b->size) {
                size_t offset = h->position - b->start, n = b->size - offset;
                if (n > size) n = size;
                memcpy(data, b->data + offset, n); h->position += n; h->hits++;
                pthread_mutex_unlock(&h->mutex);
                if (waited && milliseconds() - start >= 25)
                    log_printf("HTTP read-ahead: consumer_wait_ms=%llu bytes=%u\n",
                        (unsigned long long)(milliseconds() - start), (unsigned)n);
                return (int)n;
            }
        }
        int error = h->error;
        if (!waited) { h->waits++; waited = 1; }
        pthread_mutex_unlock(&h->mutex);
        if (error) return error;
        tick();
    }
    return -1;
}
static int64_t audio_seek(void *ctx, int64_t offset, int whence) {
    HttpAudio *h = ctx;
    pthread_mutex_lock(&h->mutex);
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? h->position :
                   whence == SEEK_END ? h->length : -1;
    int64_t next = -1;
    if (base >= 0 && offset >= -base && offset <= INT64_MAX - base && base + offset <= h->length)
        next = h->position = base + offset;
    pthread_mutex_unlock(&h->mutex);
    return next;
}
static void audio_close(void *ctx) {
    HttpAudio *h = ctx;
    audio_abort(h); pthread_join(h->worker, NULL);
    log_printf("HTTP read-ahead closed: fetches=%u reads=%u cache_misses=%u\n", h->fetches, h->hits, h->waits);
    if (h->stream.close) h->stream.close(h->stream.opaque);
    vita_https_client_destroy(h->client);
    pthread_mutex_destroy(&h->mutex); free(h->storage); free(h);
}
static int audio_open(void *ctx, VtDecoderStreamHandle *out, volatile int *cancel) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!vt_online_url_valid(ctx) ||
        (cancel && __atomic_load_n(cancel, __ATOMIC_ACQUIRE))) return -1;
    HttpAudio *h = calloc(1, sizeof(*h)); if (!h) return -1;
    VitaHttpsClientConfig config = { .user_agent = "VitaMediaDeck/1.4.2",
        .allow_http = 1, .connect_timeout_ms = 10000, .request_timeout_ms = 20000 };
    h->client = vita_https_client_create(&config);
    h->storage = malloc((SLOTS + 1) * BLOCK);
    if (!h->client || !h->storage || pthread_mutex_init(&h->mutex, NULL)) {
        vita_https_client_destroy(h->client); free(h->storage); free(h); return -1;
    }
    h->url = ctx; h->parent_cancel = cancel;
    for (int i = 0; i < SLOTS; i++) h->blocks[i].data = h->storage + i * BLOCK;
    pthread_attr_t attr;
    int result = pthread_attr_init(&attr);
    if (!result) {
        result = pthread_attr_setstacksize(&attr, 384 * 1024);
        if (!result) result = pthread_create(&h->worker, &attr, prefetch, h);
        pthread_attr_destroy(&attr);
    }
    if (result) {
        pthread_mutex_destroy(&h->mutex); vita_https_client_destroy(h->client);
        free(h->storage); free(h); return -1;
    }
    for (;;) {
        pthread_mutex_lock(&h->mutex);
        int ready = h->opened, error = h->error;
        pthread_mutex_unlock(&h->mutex);
        if (cancelled(h) || (ready && error)) { audio_close(h); return error ? error : -1; }
        if (ready) break;
        tick();
    }
    out->opaque = h; out->read = audio_read; out->seek = audio_seek;
    out->close = audio_close; out->abort = audio_abort; out->size = h->length;
    return 0;
}
static int audio_open_plain(void *ctx, VtDecoderStreamHandle *out) {
	return audio_open(ctx, out, NULL);
}
void vt_http_audio_stream_factory(const char *url, VtDecoderStreamFactory *out) {
	memset(out, 0, sizeof(*out)); out->opaque = (void *)url;
	out->open = audio_open_plain; out->open_cancelable = audio_open;
}
