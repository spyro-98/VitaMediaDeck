#ifndef VMD_IPTV_MANIFEST_H
#define VMD_IPTV_MANIFEST_H
#include <stddef.h>
#include <stdint.h>

#define VT_IPTV_URL_MAX 2048
#define VT_IPTV_PLAYLIST_LIMIT (256u * 1024u)
#define VT_IPTV_MAX_SEGMENTS 64
#define VT_IPTV_MAX_VARIANTS 16
enum {
    VT_IPTV_OK = 0, VT_IPTV_WAIT = 1, VT_IPTV_END = 2,
    VT_IPTV_DISCONTINUITY = 3, VT_IPTV_LAGGED = 4,
    VT_IPTV_INVALID = -1, VT_IPTV_UNSUPPORTED = -2,
    VT_IPTV_LIMIT = -3, VT_IPTV_IO = -4, VT_IPTV_CANCELLED = -5,
    VT_IPTV_MEMORY = -6
};
typedef struct {
    char url[VT_IPTV_URL_MAX];
    uint64_t sequence, discontinuity;
    uint32_t duration_ms;
} VtIptvSegment;
typedef struct {
    char url[VT_IPTV_URL_MAX];
    uint32_t bandwidth, width, height;
    int compatible; /* Explicit multiplexed H.264 + AAC-LC only in this gate. */
} VtIptvVariant;
typedef struct {
    VtIptvSegment segments[VT_IPTV_MAX_SEGMENTS];
    VtIptvVariant variants[VT_IPTV_MAX_VARIANTS];
    unsigned segment_count, variant_count;
    uint32_t target_ms;
    int ended;
} VtIptvManifest;

/* HTTP(S) only, no userinfo/control characters or HTTPS-to-HTTP downgrade.
 * Resolves against the final response URL, never against a pre-redirect URL. */
int vt_iptv_resolve_url(const char *base, const char *reference, char *out, size_t capacity);
/* Narrow HLS MPEG-TS subset. Unsupported required features fail explicitly;
 * this is not a generic M3U channel-list parser or a complete HLS implementation. */
int vt_iptv_manifest_parse(const void *data, size_t size, const char *base, VtIptvManifest *out);
int vt_iptv_manifest_variant(const VtIptvManifest *manifest, uint32_t max_bandwidth,
                             uint32_t max_width, uint32_t max_height);
#endif
