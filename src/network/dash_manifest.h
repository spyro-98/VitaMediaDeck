#ifndef VMD_DASH_MANIFEST_H
#define VMD_DASH_MANIFEST_H
#include "network/iptv_source.h"
#include <libxml/tree.h>

typedef struct {
    char id[128], codec[96], base[VT_IPTV_URL_MAX];
    char initialization[VT_IPTV_URL_MAX], media[VT_IPTV_URL_MAX];
    uint32_t bandwidth, width, height;
    uint64_t timescale, offset, start_number, duration;
    xmlNode *timeline, *list;
    int64_t first_us, end_us, segment_us;
    uint64_t first_tick, end_tick, count;
} VtDashTrack;
typedef struct {
    xmlDoc *document;
    VtIptvSelection active;
    VtDashTrack tracks[2]; /* H.264 video, AAC audio */
    int dynamic;
    int64_t period_us, duration_us, available_us;
    uint64_t update_ms, delay_ms;
    char period_id[128], utc_value[VT_IPTV_URL_MAX];
    int utc_http;
} VtDashManifest;
typedef struct {
    char url[VT_IPTV_URL_MAX];
    uint64_t time, duration, number;
    int64_t start_us, end_us;
} VtDashSegment;

/* Bounded single-period, unencrypted fMP4 SegmentTemplate/SegmentList subset.
 * No external XML entities, XLink, byte ranges, nested I/O or DRM. */
int vt_dash_manifest_parse(const void *data, size_t size, const char *base,
    int64_t utc_us, const VtIptvSourceConfig *limits, VtDashManifest **out);
void vt_dash_manifest_free(VtDashManifest *manifest);
/* Tick lookup includes a segment containing tick; use its end tick for next. */
int vt_dash_segment(const VtDashManifest *manifest, int track, uint64_t tick, VtDashSegment *out);
int vt_dash_start(const VtDashManifest *manifest, int track, int64_t presentation_us, VtDashSegment *out);
int vt_dash_same_tracks(const VtDashManifest *a, const VtDashManifest *b);
#endif
