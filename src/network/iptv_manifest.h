#ifndef VMD_IPTV_MANIFEST_H
#define VMD_IPTV_MANIFEST_H
#include <stddef.h>
#include <stdint.h>

#define VT_IPTV_URL_MAX 2048
#define VT_IPTV_USER_AGENT_MAX 256
#define VT_IPTV_PLAYLIST_LIMIT (256u * 1024u)
#define VT_IPTV_MAX_SEGMENTS 64
#define VT_IPTV_MAX_VARIANTS 16
enum {
    VT_IPTV_OK = 0, VT_IPTV_WAIT = 1, VT_IPTV_END = 2,
    VT_IPTV_DISCONTINUITY = 3, VT_IPTV_LAGGED = 4,
    VT_IPTV_INVALID = -1, VT_IPTV_UNSUPPORTED = -2,
    VT_IPTV_LIMIT = -3, VT_IPTV_IO = -4, VT_IPTV_CANCELLED = -5,
    VT_IPTV_MEMORY = -6, VT_IPTV_DNS = -7, VT_IPTV_ACCESS = -8,
    VT_IPTV_NOT_FOUND = -9, VT_IPTV_TIMEOUT = -10, VT_IPTV_TLS = -11,
    VT_IPTV_REDIRECT = -12, VT_IPTV_ENCRYPTED = -13,
    VT_IPTV_RENDITIONS = -14, VT_IPTV_VARIANT_LIMIT = -15,
    VT_IPTV_MANIFEST = -16, VT_IPTV_VIDEO_CODEC = -17,
    VT_IPTV_AUDIO_CODEC = -18, VT_IPTV_VIDEO_SIZE = -19,
    VT_IPTV_AUDIO_FORMAT = -20, VT_IPTV_HTTP_SERVER = -21,
    VT_IPTV_HTTP_RATE_LIMIT = -22, VT_IPTV_RANGE = -23, VT_IPTV_TIMESTAMPS = -24,
    VT_IPTV_REMUX = -25, VT_IPTV_MEDIA_DATA = -26
};
typedef struct {
    char url[VT_IPTV_URL_MAX];
    uint64_t sequence, discontinuity;
    uint32_t duration_ms;
} VtIptvSegment;
typedef struct {
    char url[VT_IPTV_URL_MAX];
    uint32_t bandwidth, width, height;
    int compatible; /* Candidate for bounded probing, not proof of codec support. */
    int codecs_declared,external_av,subtitle_group;
} VtIptvVariant;
typedef struct {
    VtIptvSegment segments[VT_IPTV_MAX_SEGMENTS];
    VtIptvVariant variants[VT_IPTV_MAX_VARIANTS];
    unsigned segment_count, variant_count;
    uint32_t target_ms;
    int ended;
    uint64_t total_duration_ms, last_sequence;
    unsigned total_segments;
} VtIptvManifest;

/* Normalize a user/QR resource URL, not a fetched manifest reference. Accept
 * outer ASCII whitespace, UTF-8 BOM, case-insensitive schemes and an optional
 * terminal C-string NUL. Ignore the client-only fragment; reject embedded NUL,
 * userinfo and internal whitespace. Never decode/alter signed paths or queries. */
int vt_iptv_input_url(const void *bytes, size_t size, char *out, size_t capacity);
/* HTTP(S) only, no userinfo/control characters or HTTPS-to-HTTP downgrade.
 * Resolves against the final response URL, never against a pre-redirect URL. */
int vt_iptv_resolve_url(const char *base, const char *reference, char *out, size_t capacity);
/* Narrow HLS MPEG-TS subset. Unsupported required features fail explicitly;
 * this is not a generic M3U channel-list parser or a complete HLS implementation. */
int vt_iptv_manifest_parse(const void *data, size_t size, const char *base, VtIptvManifest *out);
/* Finite VOD keeps the bounded original text and decodes 64-entry windows,
 * instead of allocating thousands of 2 KiB resolved segment URLs. */
int vt_iptv_manifest_parse_window(const void *data, size_t size, const char *base,
                                  uint64_t first, VtIptvManifest *out);
int vt_iptv_manifest_variant(const VtIptvManifest *manifest, uint32_t max_bandwidth,
                             uint32_t max_width, uint32_t max_height);
#endif
