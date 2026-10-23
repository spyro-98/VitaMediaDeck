#include "network/iptv_source.h"
#include "network/iptv_trace.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_SLOTS 32
#define HLS_CACHE_SLOTS 8
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
    unsigned char *vod_text;
    size_t vod_size;
    char vod_base[VT_IPTV_URL_MAX];
    int finite;
    uint64_t duration_ms;
    void *producer;
    uint32_t audio_sample_rate, audio_channels;
};
static void evict(VtIptvSource *s) {
    s->bytes -= s->chunks[0].size; free(s->chunks[0].data);
    memmove(s->chunks, s->chunks + 1, (--s->count) * sizeof(s->chunks[0]));
}
VtIptvSource *vt_iptv_source_create(const char *url, const VtIptvSourceConfig *config) {
    VtIptvSource *s = calloc(1, sizeof(*s)); if (!s) return NULL;
    if (vt_iptv_resolve_url(url, url, s->url, sizeof(s->url))) { free(s); return NULL; }
    /* One host-only identity per playback attempt: successful manifests must
     * also be attributable without persisting signed paths or query tokens. */
    const char *host=strstr(s->url,"://")+3;size_t host_length=strcspn(host,"/?#");
    if(host_length>255)host_length=255;
    IPTV_TRACE("IPTV source host=%.*s\n",(int)host_length,host);
    if (config) s->config = *config;
    if (!s->config.buffer_limit) s->config.buffer_limit = 12u * 1024u * 1024u;
    if (!s->config.segment_limit) s->config.segment_limit = 4u * 1024u * 1024u;
    if (!s->config.bandwidth_limit) s->config.bandwidth_limit = 4000000;
    if (!s->config.width_limit) s->config.width_limit = 1280;
    if (!s->config.height_limit) s->config.height_limit = 720;
    if (!s->config.fetch) s->config.fetch = vt_iptv_http_fetch;
    if ((s->config.producer.open || s->config.producer.next || s->config.producer.close) &&
        (!s->config.producer.open || !s->config.producer.next || !s->config.producer.close)) { free(s); return NULL; }
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
    if (s->producer) s->config.producer.close(s->producer);
    pthread_mutex_destroy(&s->mutex); free(s->vod_text); free(s->manifest); free(s);
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
        result == VT_IPTV_LIMIT || result == VT_IPTV_ACCESS || result == VT_IPTV_NOT_FOUND ||
        result == VT_IPTV_TLS || result == VT_IPTV_REDIRECT || result == VT_IPTV_ENCRYPTED ||
        result == VT_IPTV_RENDITIONS || result == VT_IPTV_VARIANT_LIMIT || ++s->failures >= 3) s->terminal = result;
    s->retry_at = now + 500u * s->failures;
    int terminal = s->terminal;
    IPTV_TRACE("HLS source failure: result=%d failures=%u terminal=%d buffered=%u next=%llu\n",
        result,s->failures,terminal,s->count,(unsigned long long)s->next_sequence);
    pthread_mutex_unlock(&s->mutex);
    return terminal ? terminal : VT_IPTV_WAIT;
}
static int external_poll(VtIptvSource *s, uint64_t now) {
    /* Reserve space before fetching/remuxing, exactly like the HLS path. */
    pthread_mutex_lock(&s->mutex);
    while (s->count && s->chunks[0].sequence < s->protected_sequence &&
           (s->count == CACHE_SLOTS || s->config.segment_limit > s->config.buffer_limit-s->bytes)) evict(s);
    int full = s->count == CACHE_SLOTS || s->config.segment_limit > s->config.buffer_limit-s->bytes;
    pthread_mutex_unlock(&s->mutex);
    if (full) return VT_IPTV_WAIT;
    if (!s->producer) {
        s->producer = s->config.producer.open(s->url, &s->config);
        if (!s->producer) return failure(s, VT_IPTV_MEMORY, now);
    }
    unsigned char *data = NULL; size_t size = 0;
    int finite = 0; uint64_t duration = 0;
    int result = s->config.producer.next(s->producer, now, &s->cancel, &data, &size, &finite, &duration);
    if (cancelled(s)) result = VT_IPTV_CANCELLED;
    pthread_mutex_lock(&s->mutex);
    s->finite = finite; s->duration_ms = duration;
    if(s->config.producer.audio_info)
        s->config.producer.audio_info(s->producer,&s->audio_sample_rate,&s->audio_channels);
    pthread_mutex_unlock(&s->mutex);
    if (result == VT_IPTV_OK) {
        if (!data || size < 188 || size > s->config.segment_limit || size % 188) result = VT_IPTV_LIMIT;
        else for (size_t i=0;i<size;i+=188) if(data[i]!=0x47){result=VT_IPTV_INVALID;break;}
    }
    if (result != VT_IPTV_OK) {
        free(data);
        if (result == VT_IPTV_WAIT) return result;
        /* A remuxer cannot retry a partially consumed fragment safely. Fence
         * both decoders on failure rather than concatenate different epochs. */
        pthread_mutex_lock(&s->mutex); s->terminal = result; s->last_error = result;
        pthread_mutex_unlock(&s->mutex); return result;
    }
    pthread_mutex_lock(&s->mutex);
    s->chunks[s->count++] = (Chunk){data, size, s->next_sequence++, 0};
    s->bytes += size; s->initialized = 1;
    pthread_mutex_unlock(&s->mutex);
    return VT_IPTV_OK;
}
int vt_iptv_source_poll(VtIptvSource *s, uint64_t now) {
    if (!s) return VT_IPTV_INVALID;
    if (cancelled(s)) return VT_IPTV_CANCELLED;
    pthread_mutex_lock(&s->mutex); int terminal = s->terminal; pthread_mutex_unlock(&s->mutex);
    if (terminal) return terminal;
    if (now < s->retry_at) return VT_IPTV_WAIT;
    if (s->config.producer.next) return external_poll(s, now);
    VtIptvManifest *m = s->manifest;
    if (!s->initialized || now >= s->reload_at) {
        unsigned char *body = NULL; size_t size = 0; char effective[VT_IPTV_URL_MAX];
        uint64_t started=iptv_trace_ms();
        int result = fetch(s, s->url, VT_IPTV_PLAYLIST_LIMIT, &body, &size, effective);
        if(!s->initialized || result)IPTV_TRACE("HLS playlist: fetch=%d bytes=%u elapsed=%llu ms\n",
            result,(unsigned)size,(unsigned long long)(iptv_trace_ms()-started));
        (void)started;
        if (!result) result = vt_iptv_manifest_parse_window(body, size, effective, 0, m);
        if (!result && m->variant_count) {
            free(body); body = NULL;
            if (s->initialized) return failure(s, VT_IPTV_UNSUPPORTED, now);
            int index = vt_iptv_manifest_variant(m, s->config.bandwidth_limit, s->config.width_limit, s->config.height_limit);
            IPTV_TRACE("HLS variants: count=%u selected=%d\n",m->variant_count,index);
            for(unsigned i=0;i<m->variant_count;i++) {
                const VtIptvVariant *v=&m->variants[i];
                IPTV_TRACE("HLS candidate %u: %ux%u bitrate=%u codecs_declared=%d compatible=%d external_av=%d subtitles=%d over_limit=%d\n",
                    i,v->width,v->height,v->bandwidth,v->codecs_declared,v->compatible,v->external_av,v->subtitle_group,
                    v->width>s->config.width_limit || v->height>s->config.height_limit || v->bandwidth>s->config.bandwidth_limit);
                (void)v;
            }
            if (index < 0) {
                int external=0,compatible=0;
                for(unsigned i=0;i<m->variant_count;i++){external+=m->variants[i].external_av;compatible+=m->variants[i].compatible;}
                return failure(s,compatible?VT_IPTV_VARIANT_LIMIT:external==(int)m->variant_count?VT_IPTV_RENDITIONS:VT_IPTV_UNSUPPORTED,now);
            }
            IPTV_TRACE("HLS selected: %ux%u bitrate=%u\n",m->variants[index].width,m->variants[index].height,m->variants[index].bandwidth);
            snprintf(s->url, sizeof(s->url), "%s", m->variants[index].url);
            result = fetch(s, s->url, VT_IPTV_PLAYLIST_LIMIT, &body, &size, effective);
            if (!result) result = vt_iptv_manifest_parse_window(body, size, effective, 0, m);
            if (!result && m->variant_count) result = VT_IPTV_UNSUPPORTED;
        }
        if (result) { free(body); return failure(s, result, now); }
        if(!s->initialized)IPTV_TRACE("HLS media: segments=%u target=%u ms finite=%d\n",m->total_segments,m->target_ms,m->ended);
        if (m->ended) {
            free(s->vod_text); s->vod_text = body; s->vod_size = size; body = NULL;
            snprintf(s->vod_base, sizeof(s->vod_base), "%s", effective);
        }
        free(body);
        pthread_mutex_lock(&s->mutex);
        s->finite = m->ended; s->duration_ms = m->ended ? m->total_duration_ms : 0;
        pthread_mutex_unlock(&s->mutex);
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
        s->reload_at = m->ended ? UINT64_MAX : now + (m->target_ms / 2 < 500 ? 500 : m->target_ms / 2);
    }
    if (m->ended && s->next_sequence > m->segments[m->segment_count-1].sequence && s->next_sequence <= m->last_sequence) {
        int result = vt_iptv_manifest_parse_window(s->vod_text, s->vod_size, s->vod_base, s->next_sequence, m);
        if (result) return failure(s, result, now);
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
               (s->count == HLS_CACHE_SLOTS ||
                s->config.segment_limit > s->config.buffer_limit - s->bytes))
            evict(s);
        int full = s->count == HLS_CACHE_SLOTS ||
                   s->config.segment_limit > s->config.buffer_limit - s->bytes;
        pthread_mutex_unlock(&s->mutex);
        if (full) return VT_IPTV_WAIT;
    } else pthread_mutex_unlock(&s->mutex);
    unsigned char *data = NULL; size_t size = 0; char effective[VT_IPTV_URL_MAX];
    uint64_t segment_started=iptv_trace_ms();
    int result = fetch(s, segment->url, s->config.segment_limit, &data, &size, effective);
    (void)segment_started;
    if(!s->count || result)IPTV_TRACE("HLS segment: sequence=%llu result=%d bytes=%u elapsed=%llu ms\n",
        (unsigned long long)segment->sequence,result,(unsigned)size,(unsigned long long)(iptv_trace_ms()-segment_started));
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
    while (s->count && (s->count == HLS_CACHE_SLOTS || size > s->config.buffer_limit - s->bytes)) evict(s);
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
        cancelled(s) ? VT_IPTV_CANCELLED : s->terminal, s->last_error, s->finite, s->duration_ms,
        s->audio_sample_rate, s->audio_channels, 0};
    unsigned slots = s->config.producer.next ? CACHE_SLOTS : HLS_CACHE_SLOTS;
    unsigned target = s->config.startup_segments ? s->config.startup_segments : 1;
    /* A protected startup cursor prevents eviction. Start before backpressure
     * can deadlock prefill; short VOD must also drain even below the target. */
    out->startup_ready = s->count && !cancelled(s) &&
        (s->terminal == 0 || s->terminal == VT_IPTV_END) &&
        (s->count >= target || s->count >= slots || s->terminal == VT_IPTV_END ||
         s->config.segment_limit > s->config.buffer_limit - s->bytes);
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
