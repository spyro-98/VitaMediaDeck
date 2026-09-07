#include "network/iptv_source.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_SLOTS 8
typedef struct {
    unsigned char *data;
    size_t size;
    uint64_t sequence, discontinuity;
} Chunk;
struct VtIptvSource {
    pthread_mutex_t mutex;
    VtIptvSourceConfig config;
    VtIptvManifest *manifest;
    Chunk chunks[CACHE_SLOTS];
    unsigned count, failures;
    size_t bytes;
    uint64_t generation, next_sequence, reload_at, retry_at;
    uint64_t protected_sequence;
    int initialized, terminal, last_error;
    volatile int cancel;
    char url[VT_IPTV_URL_MAX];
};
static void evict(VtIptvSource *s) {
    s->bytes -= s->chunks[0].size; free(s->chunks[0].data);
    memmove(s->chunks, s->chunks + 1, (--s->count) * sizeof(s->chunks[0]));
}
VtIptvSource *vt_iptv_source_create(const char *url, const VtIptvSourceConfig *config) {
    VtIptvSource *s = calloc(1, sizeof(*s)); if (!s) return NULL;
    if (vt_iptv_resolve_url(url, url, s->url, sizeof(s->url))) { free(s); return NULL; }
    if (config) s->config = *config;
    if (!s->config.buffer_limit) s->config.buffer_limit = 12u * 1024u * 1024u;
    if (!s->config.segment_limit) s->config.segment_limit = 4u * 1024u * 1024u;
    if (!s->config.bandwidth_limit) s->config.bandwidth_limit = 4000000;
    if (!s->config.width_limit) s->config.width_limit = 1280;
    if (!s->config.height_limit) s->config.height_limit = 720;
    if (!s->config.fetch) s->config.fetch = vt_iptv_http_fetch;
    if (s->config.buffer_limit > 32u * 1024u * 1024u ||
        s->config.segment_limit > s->config.buffer_limit || s->config.segment_limit < 188) { free(s); return NULL; }
    s->manifest = calloc(1, sizeof(*s->manifest));
    if (!s->manifest || pthread_mutex_init(&s->mutex, NULL)) { free(s->manifest); free(s); return NULL; }
    s->generation = 1;
    s->protected_sequence = UINT64_MAX;
    return s;
}
static int cancelled(const VtIptvSource *s) {
    return __atomic_load_n(&s->cancel, __ATOMIC_ACQUIRE);
}
void vt_iptv_source_cancel(VtIptvSource *s) {
    if (s) __atomic_store_n(&s->cancel, 1, __ATOMIC_RELEASE);
}
void vt_iptv_source_destroy(VtIptvSource *s) {
    if (!s) return;
    while (s->count) evict(s);
    pthread_mutex_destroy(&s->mutex); free(s->manifest); free(s);
}
static int fetch(VtIptvSource *s, const char *url, size_t limit, unsigned char **data, size_t *size, char *effective) {
    *data = NULL; *size = 0; effective[0] = 0;
    if (cancelled(s)) return VT_IPTV_CANCELLED;
    int result = s->config.fetch(s->config.fetch_ctx, url, limit, data, size,
        effective, VT_IPTV_URL_MAX, &s->cancel);
    if (cancelled(s)) return VT_IPTV_CANCELLED;
    if (result) return result;
    if (!*data || !*size || *size > limit) return VT_IPTV_LIMIT;
    char validated[VT_IPTV_URL_MAX];
    return vt_iptv_resolve_url(url, effective, validated, sizeof(validated));
}
static unsigned live_start(const VtIptvManifest *m) {
    if (m->ended) return 0;
    uint64_t holdback = (uint64_t)m->target_ms * 3, duration = 0;
    unsigned index = m->segment_count;
    while (index && duration < holdback) duration += m->segments[--index].duration_ms;
    return index;
}
static int failure(VtIptvSource *s, int result, uint64_t now) {
    pthread_mutex_lock(&s->mutex);
    s->last_error = result;
    if (result == VT_IPTV_CANCELLED || result == VT_IPTV_UNSUPPORTED || result == VT_IPTV_INVALID ||
        result == VT_IPTV_LIMIT || ++s->failures >= 3) s->terminal = result;
    s->retry_at = now + 500u * s->failures;
    int terminal = s->terminal;
    pthread_mutex_unlock(&s->mutex);
    return terminal ? terminal : VT_IPTV_WAIT;
}
int vt_iptv_source_poll(VtIptvSource *s, uint64_t now) {
    if (!s) return VT_IPTV_INVALID;
    if (cancelled(s)) return VT_IPTV_CANCELLED;
    pthread_mutex_lock(&s->mutex); int terminal = s->terminal; pthread_mutex_unlock(&s->mutex);
    if (terminal) return terminal;
    if (now < s->retry_at) return VT_IPTV_WAIT;
    VtIptvManifest *m = s->manifest;
    if (!s->initialized || now >= s->reload_at) {
        unsigned char *body = NULL; size_t size = 0; char effective[VT_IPTV_URL_MAX];
        int result = fetch(s, s->url, VT_IPTV_PLAYLIST_LIMIT, &body, &size, effective);
        if (!result) result = vt_iptv_manifest_parse(body, size, effective, m);
        free(body);
        if (!result && m->variant_count) {
            if (s->initialized) return failure(s, VT_IPTV_UNSUPPORTED, now);
            int index = vt_iptv_manifest_variant(m, s->config.bandwidth_limit, s->config.width_limit, s->config.height_limit);
            if (index < 0) return failure(s, VT_IPTV_UNSUPPORTED, now);
            snprintf(s->url, sizeof(s->url), "%s", m->variants[index].url);
            result = fetch(s, s->url, VT_IPTV_PLAYLIST_LIMIT, &body, &size, effective);
            if (!result) result = vt_iptv_manifest_parse(body, size, effective, m);
            free(body);
            if (!result && m->variant_count) result = VT_IPTV_UNSUPPORTED;
        }
        if (result) return failure(s, result, now);
        /* Keep requesting the playlist's stable URL (redirect tokens may expire),
         * but resolve segment URLs against each final response URL. */
        if (!s->initialized) {
            pthread_mutex_lock(&s->mutex);
            s->next_sequence = m->segments[live_start(m)].sequence; s->initialized = 1;
            pthread_mutex_unlock(&s->mutex);
        } else if (s->next_sequence < m->segments[0].sequence ||
                   s->next_sequence > m->segments[m->segment_count - 1].sequence + 1) {
            pthread_mutex_lock(&s->mutex);
            while (s->count) evict(s);
            s->generation++; s->next_sequence = m->segments[live_start(m)].sequence;
            pthread_mutex_unlock(&s->mutex);
        }
        s->reload_at = now + (m->target_ms / 2 < 500 ? 500 : m->target_ms / 2);
    }
    VtIptvSegment *segment = NULL;
    for (unsigned i = 0; i < m->segment_count; i++) if (m->segments[i].sequence == s->next_sequence) segment = &m->segments[i];
    if (!segment) {
        if (m->ended) { pthread_mutex_lock(&s->mutex); s->terminal = VT_IPTV_END; pthread_mutex_unlock(&s->mutex); return VT_IPTV_END; }
        return VT_IPTV_WAIT;
    }
    pthread_mutex_lock(&s->mutex);
    /* Reserve for the maximum incoming segment BEFORE the HTTP request:
     * backpressure must not cause repeated fetch/discard of the same segment. */
    if (s->protected_sequence != UINT64_MAX) {
        while (s->count && s->chunks[0].sequence < s->protected_sequence &&
               (s->count == CACHE_SLOTS ||
                s->config.segment_limit > s->config.buffer_limit - s->bytes))
            evict(s);
        int full = s->count == CACHE_SLOTS ||
                   s->config.segment_limit > s->config.buffer_limit - s->bytes;
        pthread_mutex_unlock(&s->mutex);
        if (full) return VT_IPTV_WAIT;
    } else pthread_mutex_unlock(&s->mutex);
    unsigned char *data = NULL; size_t size = 0; char effective[VT_IPTV_URL_MAX];
    int result = fetch(s, segment->url, s->config.segment_limit, &data, &size, effective);
    if (!result) {
        /* This gate accepts complete, packet-aligned MPEG-TS segments only. */
        if (size < 188 || size % 188) result = VT_IPTV_UNSUPPORTED;
        else for (size_t i = 0; i < size; i += 188) if (data[i] != 0x47) { result = VT_IPTV_UNSUPPORTED; break; }
    }
    if (result) { free(data); return failure(s, result, now); }
    /* Fetch buffers grow geometrically. Retain only their actual payload size
     * so the cache byte budget also bounds retained allocations. In-flight
     * response memory is separately capped by segment_limit. */
    unsigned char *exact = realloc(data, size);
    if (!exact) { free(data); return failure(s, VT_IPTV_MEMORY, now); }
    data = exact;
    pthread_mutex_lock(&s->mutex);
    while (s->count && (s->count == CACHE_SLOTS || size > s->config.buffer_limit - s->bytes)) evict(s);
    s->chunks[s->count++] = (Chunk){data, size, segment->sequence, segment->discontinuity};
    s->bytes += size; s->next_sequence++; s->failures = 0; s->last_error = 0;
    pthread_mutex_unlock(&s->mutex);
    return VT_IPTV_OK;
}
void vt_iptv_source_status(VtIptvSource *s, VtIptvSourceStatus *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out)); if (!s) { out->terminal = VT_IPTV_INVALID; return; }
    pthread_mutex_lock(&s->mutex);
    *out = (VtIptvSourceStatus){s->count ? s->chunks[0].sequence : s->next_sequence,
        s->next_sequence, s->generation, s->bytes, s->count, s->failures,
        cancelled(s) ? VT_IPTV_CANCELLED : s->terminal, s->last_error};
    pthread_mutex_unlock(&s->mutex);
}
void vt_iptv_source_protect(VtIptvSource *s, uint64_t sequence) {
    if (!s) return;
    pthread_mutex_lock(&s->mutex);
    s->protected_sequence = sequence;
    pthread_mutex_unlock(&s->mutex);
}
int vt_iptv_source_reanchor(VtIptvSource *s, uint64_t sequence, VtIptvCursor *cursor) {
    if (!s || !cursor) return VT_IPTV_INVALID;
    int result = VT_IPTV_LAGGED;
    pthread_mutex_lock(&s->mutex);
    for (unsigned i = 0; i < s->count; i++) if (s->chunks[i].sequence == sequence) {
        *cursor = (VtIptvCursor){s->generation, sequence, s->chunks[i].discontinuity, 0}; result = VT_IPTV_OK; break;
    }
    pthread_mutex_unlock(&s->mutex); return cancelled(s) ? VT_IPTV_CANCELLED : result;
}
int vt_iptv_source_begin(VtIptvSource *s, VtIptvCursor *cursor) {
    if (!s || !cursor) return VT_IPTV_INVALID;
    pthread_mutex_lock(&s->mutex);
    int result = s->count ? VT_IPTV_OK : s->terminal ? s->terminal : VT_IPTV_WAIT;
    if (s->count) *cursor = (VtIptvCursor){s->generation, s->chunks[0].sequence, s->chunks[0].discontinuity, 0};
    pthread_mutex_unlock(&s->mutex); return cancelled(s) ? VT_IPTV_CANCELLED : result;
}
int vt_iptv_source_read(VtIptvSource *s, VtIptvCursor *cursor, void *data, size_t capacity, size_t *bytes) {
    if (bytes) *bytes = 0;
    if (!s || !cursor || !data || !capacity || !bytes) return VT_IPTV_INVALID;
    if (cancelled(s)) return VT_IPTV_CANCELLED;
    pthread_mutex_lock(&s->mutex);
    int result = s->terminal ? s->terminal : VT_IPTV_WAIT;
    if (cursor->generation != s->generation) result = VT_IPTV_DISCONTINUITY;
    else if (s->count && cursor->sequence < s->chunks[0].sequence) result = VT_IPTV_LAGGED;
    else for (unsigned i = 0; i < s->count; i++) {
        Chunk *chunk = &s->chunks[i]; if (chunk->sequence != cursor->sequence) continue;
        if (chunk->discontinuity != cursor->discontinuity) result = VT_IPTV_DISCONTINUITY;
        else if (cursor->offset > chunk->size) result = VT_IPTV_INVALID;
        else {
            *bytes = chunk->size - cursor->offset; if (*bytes > capacity) *bytes = capacity;
            memcpy(data, chunk->data + cursor->offset, *bytes); cursor->offset += *bytes;
            if (cursor->offset == chunk->size) { cursor->sequence++; cursor->offset = 0; }
            result = *bytes ? VT_IPTV_OK : VT_IPTV_WAIT;
        }
        break;
    }
    pthread_mutex_unlock(&s->mutex); return result;
}
