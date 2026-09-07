#ifndef VMD_IPTV_SOURCE_H
#define VMD_IPTV_SOURCE_H
#include "network/iptv_manifest.h"

typedef struct VtIptvSource VtIptvSource;
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
typedef struct {
    size_t buffer_limit, segment_limit;
    uint32_t bandwidth_limit, width_limit, height_limit;
    VtIptvFetch fetch;
    void *fetch_ctx;
} VtIptvSourceConfig;
typedef struct {
    uint64_t first_sequence, next_sequence, generation;
    size_t buffered_bytes;
    unsigned buffered_segments, consecutive_failures;
    int terminal, last_error;
} VtIptvSourceStatus;

/* Defaults: 12 MiB compressed cache, 4 MiB/segment, 4 Mbps and 1280x720 variant
 * filters. These are conservative test policies, not proven Vita decoder limits. */
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
#endif
