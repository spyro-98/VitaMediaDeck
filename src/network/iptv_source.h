#ifndef VMD_IPTV_SOURCE_H
#define VMD_IPTV_SOURCE_H
#include "network/iptv_manifest.h"
#include "network/iptv_options.h"

typedef struct VtIptvSource VtIptvSource;
typedef struct { char user_agent[VT_IPTV_USER_AGENT_MAX]; int allow_local; } VtIptvHttpOptions;
typedef struct {
    uint64_t generation, sequence, discontinuity;
    size_t offset;
} VtIptvCursor;
/* A fetch allocates *data with malloc, even on a partial failure; source frees
 * it. Enforce limit on received bytes and return the final, verified URL.
 * Callbacks must observe cancel. No playback/decoder API is called here. */
typedef int (*VtIptvFetch)(void *ctx, const char *url, size_t limit,
    unsigned char **data, size_t *size, char *effective, size_t effective_capacity,
    volatile int *cancel);
typedef int (*VtIptvFetchRange)(void *ctx, const char *url, uint64_t offset,
    size_t length, unsigned char **data, size_t *size, char *effective,
    size_t effective_capacity, volatile int *cancel);
typedef struct VtIptvSourceConfig VtIptvSourceConfig;
/* Optional app-side transport/remux producer. open/next run on the producer
 * thread; close runs after it is joined. next returns malloc-owned MPEG-TS chunks.
 * The common source still owns cancellation, backpressure and A/V cursors. */
typedef struct {
    void *(*open)(const char *url, const VtIptvSourceConfig *config);
    int (*next)(void *ctx, uint64_t now_ms, volatile int *cancel,
                unsigned char **data, size_t *size, int *finite, uint64_t *duration_ms);
    void (*close)(void *ctx);
    void (*audio_info)(void *ctx, uint32_t *sample_rate, uint32_t *channels);
} VtIptvProducer;
struct VtIptvSourceConfig {
    size_t buffer_limit, segment_limit;
    uint32_t bandwidth_limit, width_limit, height_limit;
    VtIptvFetch fetch;
    void *fetch_ctx;
    VtIptvProducer producer; /* All-zero retains the original HLS path. */
    unsigned startup_segments; /* Zero preserves one-chunk startup. */
    VtIptvSelection selection;
    VtIptvSelection *active_selection; /* Producer-private, initialization only. */
    VtIptvOptions *stream_options; /* HLS init writes once, before publishing its first chunk. */
    VtIptvOptions *inspect; /* Only synchronous discovery may write this. NULL in sessions. */
    VtIptvFetchRange fetch_range; /* Optional verified exact HTTP byte ranges. */
};
typedef struct {
    uint64_t first_sequence, next_sequence, generation;
    size_t buffered_bytes;
    unsigned buffered_segments, consecutive_failures;
    int terminal, last_error;
    int finite;
    uint64_t duration_ms;
    uint32_t audio_sample_rate, audio_channels;
    int startup_ready; /* Target reached, cache full, or buffered short VOD. */
} VtIptvSourceStatus;

/* Defaults: 12 MiB compressed cache, 4 MiB/segment, no default bitrate or resolution rejection.
 * Explicit caller limits are optional policies, not decoder capabilities. */
VtIptvSource *vt_iptv_source_create(const char *url, const VtIptvSourceConfig *config);
/* One producer calls poll on a worker, with a monotonic millisecond clock.
 * It performs at most one segment fetch per call. WAIT is not EOF; retries
 * are bounded to three consecutive failures. Consumers never do network I/O. */
int vt_iptv_source_poll(VtIptvSource *source, uint64_t now_ms);
void vt_iptv_source_cancel(VtIptvSource *source);
/* Cancel and join the producer/consumers before destroy. */
void vt_iptv_source_destroy(VtIptvSource *source);
void vt_iptv_source_status(VtIptvSource *source, VtIptvSourceStatus *out);
/* Optional consumer backpressure: retain this sequence and newer chunks.
 * UINT64_MAX restores the original evict-oldest policy. A caller must advance
 * the floor as ALL consumers pass it. Playlist resets still signal a generation
 * change; a stalled reader never silently rejoins a different timeline. */
void vt_iptv_source_protect(VtIptvSource *source, uint64_t sequence);
/* Copy ONE begin cursor to the A/V consumers to establish the same landing. */
int vt_iptv_source_begin(VtIptvSource *source, VtIptvCursor *cursor);
/* Explicitly re-anchor only after coordinated decoder/clock flush. */
int vt_iptv_source_reanchor(VtIptvSource *source, uint64_t sequence, VtIptvCursor *cursor);
/* Separate result and byte count distinguish WAIT/END/errors/discontinuities.
 * Never map WAIT, LAGGED or DISCONTINUITY to a zero-byte EOF in an adapter. */
int vt_iptv_source_read(VtIptvSource *source, VtIptvCursor *cursor,
                         void *data, size_t capacity, size_t *bytes);
int vt_iptv_http_fetch(void *ctx, const char *url, size_t limit,
    unsigned char **data, size_t *size, char *effective, size_t effective_capacity,
    volatile int *cancel);
int vt_iptv_http_fetch_range(void *ctx, const char *url, uint64_t offset,
    size_t length, unsigned char **data, size_t *size, char *effective,
    size_t effective_capacity, volatile int *cancel);
#endif
