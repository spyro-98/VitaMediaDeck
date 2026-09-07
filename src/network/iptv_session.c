#include "network/iptv_session.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#endif

typedef struct LiveCursor {
    struct VtIptvSession *session;
    VtIptvCursor position;
    volatile int *operation_cancel;
    int aborted;
} LiveCursor;
struct VtIptvSession {
    VtIptvSource *source;
    pthread_t worker;
    pthread_mutex_t mutex;
    LiveCursor *readers[2];
    VtIptvCursor anchor;
    int anchor_valid, paired;
    int cancelled, event;
};
static uint64_t now_ms(void) {
#ifdef __vita__
    return sceKernelGetProcessTimeWide() / 1000;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}
static void sleep_tick(void) {
#ifdef __vita__
    sceKernelDelayThread(5000);
#else
    struct timespec delay = {0, 5000000};
    nanosleep(&delay, NULL);
#endif
}
static int load_flag(const int *flag) { return __atomic_load_n(flag, __ATOMIC_ACQUIRE); }
static void signal_event(VtIptvSession *s, int event) {
    int expected = 0;
    __atomic_compare_exchange_n(&s->event, &expected, event, 0,
                                __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}
/* Called with session mutex. The first cursor's probe may consume substantial
 * data before the second is opened. Pin the original anchor until both exist. */
static void update_floor(VtIptvSession *s) {
    uint64_t floor = s->anchor_valid ? s->anchor.sequence : 0;
    if (s->paired) {
        floor = UINT64_MAX;
        for (int i = 0; i < 2; i++)
            if (s->readers[i] && s->readers[i]->position.sequence < floor)
                floor = s->readers[i]->position.sequence;
    }
    vt_iptv_source_protect(s->source, floor);
}
static void *produce(void *opaque) {
    VtIptvSession *s = opaque;
    while (!load_flag(&s->cancelled)) {
        int result = vt_iptv_source_poll(s->source, now_ms());
        if (result < 0) {
            if (result != VT_IPTV_CANCELLED) signal_event(s, result);
            break;
        }
        if (result == VT_IPTV_END) break;
        sleep_tick();
    }
    return NULL;
}
VtIptvSession *vt_iptv_session_create(const char *url, const VtIptvSourceConfig *config) {
    VtIptvSession *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->source = vt_iptv_source_create(url, config);
    if (!s->source || pthread_mutex_init(&s->mutex, NULL)) {
        vt_iptv_source_destroy(s->source); free(s); return NULL;
    }
    vt_iptv_source_protect(s->source, 0);
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)) {
        pthread_mutex_destroy(&s->mutex); vt_iptv_source_destroy(s->source); free(s); return NULL;
    }
    int result = pthread_attr_setstacksize(&attr, 384 * 1024);
    if (!result) result = pthread_create(&s->worker, &attr, produce, s);
    pthread_attr_destroy(&attr);
    if (result) {
        pthread_mutex_destroy(&s->mutex); vt_iptv_source_destroy(s->source); free(s); return NULL;
    }
    return s;
}
void vt_iptv_session_cancel(VtIptvSession *s) {
    if (!s) return;
    __atomic_store_n(&s->cancelled, 1, __ATOMIC_RELEASE);
    vt_iptv_source_cancel(s->source);
}
void vt_iptv_session_destroy(VtIptvSession *s) {
    if (!s) return;
    vt_iptv_session_cancel(s);
    pthread_join(s->worker, NULL);
    vt_iptv_source_destroy(s->source);
    pthread_mutex_destroy(&s->mutex); free(s);
}
int vt_iptv_session_event(VtIptvSession *s) {
    if (!s) return VT_IPTV_INVALID;
    return load_flag(&s->cancelled) ? VT_IPTV_CANCELLED : load_flag(&s->event);
}
void vt_iptv_session_status(VtIptvSession *s, VtIptvSourceStatus *out) {
    vt_iptv_source_status(s ? s->source : NULL, out);
}
static int interrupted(LiveCursor *c) {
    return load_flag(&c->aborted) || load_flag(&c->session->cancelled) ||
           (c->operation_cancel && *c->operation_cancel);
}
static void abort_cursor(void *opaque) {
    LiveCursor *c = opaque;
    if (c) __atomic_store_n(&c->aborted, 1, __ATOMIC_RELEASE);
}
static void close_cursor(void *opaque) {
    LiveCursor *c = opaque; if (!c) return;
    VtIptvSession *s = c->session;
    pthread_mutex_lock(&s->mutex);
    for (int i = 0; i < 2; i++) if (s->readers[i] == c) s->readers[i] = NULL;
    if (!s->readers[0] && !s->readers[1]) s->anchor_valid = s->paired = 0;
    update_floor(s);
    pthread_mutex_unlock(&s->mutex);
    free(c);
}
static int read_cursor(void *opaque, void *data, size_t capacity) {
    LiveCursor *c = opaque;
    if (!c || !data || !capacity) return -1;
    if (capacity > INT_MAX) capacity = INT_MAX;
    VtIptvSession *s = c->session;
    uint64_t deadline = now_ms() + 25000;
    for (;;) {
        if (interrupted(c) || load_flag(&s->event)) return -1;
        size_t bytes = 0;
        pthread_mutex_lock(&s->mutex);
        int result = vt_iptv_source_read(s->source, &c->position, data, capacity, &bytes);
        if (bytes) update_floor(s);
        pthread_mutex_unlock(&s->mutex);
        if (result == VT_IPTV_OK && bytes) return (int)bytes;
        if (result == VT_IPTV_END) return 0; /* Only a real, drained ENDLIST. */
        if (result != VT_IPTV_WAIT) {
            if (result != VT_IPTV_CANCELLED) signal_event(s, result);
            return -1;
        }
        if (now_ms() >= deadline) { signal_event(s, VT_IPTV_IO); return -1; }
        sleep_tick();
    }
}
static int open_cursor(void *opaque, VtDecoderStreamHandle *out, volatile int *cancel) {
    VtIptvSession *s = opaque;
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));
    LiveCursor *c = calloc(1, sizeof(*c)); if (!c) return -1;
    c->session = s; c->operation_cancel = cancel;
    uint64_t deadline = now_ms() + 25000;
    for (;;) {
        if (interrupted(c) || load_flag(&s->event)) break;
        pthread_mutex_lock(&s->mutex);
        int slot = !s->readers[0] ? 0 : !s->readers[1] ? 1 : -1;
        int result = slot < 0 ? VT_IPTV_LIMIT : VT_IPTV_OK;
        if (!result && !s->anchor_valid) {
            result = vt_iptv_source_begin(s->source, &s->anchor);
            if (!result) s->anchor_valid = 1;
        }
        if (!result) {
            c->position = s->anchor;
            s->readers[slot] = c;
            if (s->readers[0] && s->readers[1]) s->paired = 1;
            update_floor(s);
        }
        pthread_mutex_unlock(&s->mutex);
        if (!result) {
            *out = (VtDecoderStreamHandle){.opaque = c, .read = read_cursor,
                .close = close_cursor, .abort = abort_cursor, .size = -1};
            return 0; /* Deliberately NULL seek: never fake a file. */
        }
        if (result != VT_IPTV_WAIT || now_ms() >= deadline) break;
        sleep_tick();
    }
    free(c); return -1;
}
static int open_uncancelable(void *opaque, VtDecoderStreamHandle *out) {
    return open_cursor(opaque, out, NULL);
}
void vt_iptv_session_factory(VtIptvSession *s, VtDecoderStreamFactory *out) {
    if (!out) return;
    *out = (VtDecoderStreamFactory){.opaque = s,
        .open = open_uncancelable, .open_cancelable = open_cursor};
}
