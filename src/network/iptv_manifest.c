#include "network/iptv_manifest.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libxml/uri.h>
#include <libxml/xmlmemory.h>

static int safe_url(const char *url) {
    if (!url || !*url || strlen(url) >= VT_IPTV_URL_MAX) return 0;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p <= 32 || *p == 127 || *p == '\\') return 0;
    xmlURI *uri = xmlParseURI(url);
    int valid = uri && uri->scheme && uri->server && *uri->server && !uri->user && !uri->fragment &&
        (!strcasecmp(uri->scheme, "https") || !strcasecmp(uri->scheme, "http"));
    xmlFreeURI(uri); return valid;
}
int vt_iptv_resolve_url(const char *base, const char *ref, char *out, size_t capacity) {
    if (!out || !capacity) return VT_IPTV_INVALID;
    out[0] = 0;
    if (!safe_url(base) || !ref || !*ref || strlen(ref) >= VT_IPTV_URL_MAX) return VT_IPTV_INVALID;
    for (const unsigned char *p = (const unsigned char *)ref; *p; p++)
        if (*p <= 32 || *p == 127 || *p == '\\') return VT_IPTV_INVALID;
    xmlChar *absolute = xmlBuildURI((const xmlChar *)ref, (const xmlChar *)base);
    int result = VT_IPTV_INVALID;
    if (absolute && safe_url((const char *)absolute) &&
        (strncasecmp(base, "https://", 8) || !strncasecmp((const char *)absolute, "https://", 8))) {
        size_t size = strlen((const char *)absolute);
        if (size >= capacity) result = VT_IPTV_LIMIT;
        else { memcpy(out, absolute, size + 1); result = VT_IPTV_OK; }
    }
    xmlFree(absolute); return result;
}
static int number(const char *text, uint64_t *out) {
    if (!text || !isdigit((unsigned char)*text)) return 0;
    errno = 0; char *end;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno || *end) return 0;
    *out = n; return 1;
}
/* Attribute values can contain commas inside double quotes (notably CODECS). */
static int attribute(const char *text, const char *key, char *out, size_t capacity) {
    int found = 0; out[0] = 0;
    while (*text) {
        const char *name = text;
        while (*text && *text != '=' && *text != ',') text++;
        if (*text != '=') return -1;
        size_t name_size = (size_t)(text - name); text++;
        int quoted = *text == '"'; if (quoted) text++;
        const char *value = text;
        while (*text && (quoted ? *text != '"' : *text != ',')) text++;
        size_t length = (size_t)(text - value);
        if (quoted) { if (*text != '"') return -1; text++; }
        if (*text && *text != ',') return -1;
        if (name_size == strlen(key) && !memcmp(name, key, name_size)) {
            if (found || length >= capacity) return -1;
            memcpy(out, value, length); out[length] = 0; found = 1;
        }
        if (*text) { text++; if (!*text) return -1; }
    }
    return found;
}
static int codecs_supported(const char *text) {
    int video = 0, audio = 0;
    while (*text) {
        const char *end = strchr(text, ','); size_t n = end ? (size_t)(end - text) : strlen(text);
        if (n == 11 && !memcmp(text, "avc1.", 5)) {
            for (size_t i = 5; i < n; i++) if (!isxdigit((unsigned char)text[i])) return 0;
            /* Baseline, Main or 8-bit High; profile is not a device capability guarantee. */
            if (strncasecmp(text + 5, "42", 2) && strncasecmp(text + 5, "4d", 2) && strncasecmp(text + 5, "64", 2)) return 0;
            video++;
        } else if (n == 9 && !memcmp(text, "mp4a.40.2", 9)) audio++;
        else return 0;
        if (!end) break;
        text = end + 1;
    }
    return video == 1 && audio == 1;
}
static int variant(const char *text, VtIptvVariant *out) {
    char bandwidth[24], resolution[48], codecs[128], group[128]; uint64_t n;
    memset(out, 0, sizeof(*out));
    if (attribute(text, "BANDWIDTH", bandwidth, sizeof(bandwidth)) != 1 || !number(bandwidth, &n) || !n || n > UINT32_MAX) return VT_IPTV_INVALID;
    out->bandwidth = (uint32_t)n;
    int has_resolution = attribute(text, "RESOLUTION", resolution, sizeof(resolution));
    int has_codecs = attribute(text, "CODECS", codecs, sizeof(codecs));
    if (has_resolution < 0 || has_codecs < 0) return VT_IPTV_INVALID;
    if (has_resolution) {
        char *separator = strchr(resolution, 'x');
        if (!separator) return VT_IPTV_INVALID;
        *separator++ = 0;
        if (!number(resolution, &n) || !n || n > UINT32_MAX) return VT_IPTV_INVALID;
        out->width = (uint32_t)n;
        if (!number(separator, &n) || !n || n > UINT32_MAX) return VT_IPTV_INVALID;
        out->height = (uint32_t)n;
    }
    out->compatible = has_codecs && has_resolution && codecs_supported(codecs);
    const char *groups[] = {"AUDIO", "VIDEO", "SUBTITLES"};
    for (unsigned i = 0; i < 3; i++) {
        int present = attribute(text, groups[i], group, sizeof(group));
        if (present < 0) return VT_IPTV_INVALID;
        if (present) out->compatible = 0; /* Separate rendition synchronization is a later gate. */
    }
    return VT_IPTV_OK;
}
int vt_iptv_manifest_parse(const void *bytes, size_t size, const char *base, VtIptvManifest *out) {
    if (!out) return VT_IPTV_INVALID;
    memset(out, 0, sizeof(*out));
    if (!bytes || !size || size > VT_IPTV_PLAYLIST_LIMIT || memchr(bytes, 0, size) || !safe_url(base)) return VT_IPTV_INVALID;
    const char *data = bytes; size_t offset = 0;
    uint64_t sequence = 0, discontinuity = 0;
    int header = 0, pending = 0, seq_seen = 0, disc_seen = 0, target_seen = 0, version_seen = 0, disc_tag_seen = 0;
    uint32_t duration = 0; VtIptvVariant candidate = {0};
    while (offset < size) {
        size_t start = offset;
        while (offset < size && data[offset] != '\n') offset++;
        size_t length = offset - start; if (offset < size) offset++;
        if (length && data[start + length - 1] == '\r') length--;
        if (length >= 4096) return VT_IPTV_LIMIT;
        char line[4096]; memcpy(line, data + start, length); line[length] = 0;
        if (!length) continue;
        if (!header) { if (strcmp(line, "#EXTM3U")) return VT_IPTV_INVALID; header = 1; continue; }
        if (!strcmp(line, "#EXTM3U")) return VT_IPTV_INVALID;
        if (!strncmp(line, "#EXT-X-VERSION:", 15)) {
            uint64_t version;
            if (version_seen++ || !number(line + 15, &version) || !version) return VT_IPTV_INVALID;
            if (version > 7) return VT_IPTV_UNSUPPORTED;
        } else if (!strncmp(line, "#EXT-X-STREAM-INF:", 18)) {
            if (pending || out->segment_count || target_seen || seq_seen || disc_seen || disc_tag_seen || out->ended) return VT_IPTV_INVALID;
            int result = variant(line + 18, &candidate); if (result) return result;
            pending = 2;
        } else if (!strncmp(line, "#EXTINF:", 8)) {
            if (pending || out->variant_count || out->ended) return VT_IPTV_INVALID;
            char *end; errno = 0; double seconds = strtod(line + 8, &end);
            if (errno || !isdigit((unsigned char)line[8]) || end == line + 8 || *end != ',' || !isfinite(seconds) || seconds <= 0 || seconds > 120) return VT_IPTV_INVALID;
            duration = (uint32_t)(seconds * 1000.0 + 0.5); if (!duration) return VT_IPTV_INVALID;
            pending = 1;
        } else if (!strncmp(line, "#EXT-X-TARGETDURATION:", 22)) {
            uint64_t n;
            if (target_seen++ || !number(line + 22, &n) || !n || n > 120 || out->variant_count) return VT_IPTV_INVALID;
            out->target_ms = (uint32_t)n * 1000;
        } else if (!strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22)) {
            if (seq_seen++ || out->segment_count || out->variant_count || pending == 2 || !number(line + 22, &sequence)) return VT_IPTV_INVALID;
        } else if (!strncmp(line, "#EXT-X-DISCONTINUITY-SEQUENCE:", 30)) {
            if (disc_seen++ || disc_tag_seen || out->segment_count || out->variant_count || pending == 2 || !number(line + 30, &discontinuity)) return VT_IPTV_INVALID;
        } else if (!strcmp(line, "#EXT-X-DISCONTINUITY")) {
            if (discontinuity == UINT64_MAX || out->variant_count || pending == 2) return VT_IPTV_INVALID;
            discontinuity++; disc_tag_seen = 1;
        } else if (!strcmp(line, "#EXT-X-ENDLIST")) {
            if (pending || out->ended || out->variant_count) return VT_IPTV_INVALID;
            out->ended = 1;
        } else if (!strncmp(line, "#EXT-X-KEY:", 11)) {
            if (strcmp(line + 11, "METHOD=NONE")) return VT_IPTV_UNSUPPORTED;
        } else if (!strncmp(line, "#EXT-X-MAP:", 11) || !strncmp(line, "#EXT-X-BYTERANGE:", 17) ||
                   !strncmp(line, "#EXT-X-PART", 11) || !strncmp(line, "#EXT-X-SKIP:", 12) ||
                   !strncmp(line, "#EXT-X-SESSION-KEY:", 19) || !strncmp(line, "#EXT-X-DEFINE:", 14) ||
                   !strcmp(line, "#EXT-X-GAP") || !strcmp(line, "#EXT-X-I-FRAMES-ONLY")) return VT_IPTV_UNSUPPORTED;
        else if (line[0] != '#') {
            if (!pending || out->ended) return VT_IPTV_INVALID;
            if (pending == 2) {
                if (out->variant_count == VT_IPTV_MAX_VARIANTS) return VT_IPTV_LIMIT;
                int result = vt_iptv_resolve_url(base, line, candidate.url, sizeof(candidate.url)); if (result) return result;
                out->variants[out->variant_count++] = candidate;
            } else {
                if (out->segment_count == VT_IPTV_MAX_SEGMENTS) return VT_IPTV_LIMIT;
                if (sequence == UINT64_MAX) return VT_IPTV_INVALID;
                VtIptvSegment *segment = &out->segments[out->segment_count];
                int result = vt_iptv_resolve_url(base, line, segment->url, sizeof(segment->url)); if (result) return result;
                segment->sequence = sequence++; segment->discontinuity = discontinuity; segment->duration_ms = duration;
                out->segment_count++;
            }
            pending = 0;
        }
    }
    if (!header || pending || (!out->segment_count && !out->variant_count)) return VT_IPTV_INVALID;
    if (out->segment_count && !target_seen) return VT_IPTV_INVALID;
    for (unsigned i = 0; i < out->segment_count; i++)
        if ((out->segments[i].duration_ms + 500) / 1000 > out->target_ms / 1000) return VT_IPTV_INVALID;
    return VT_IPTV_OK;
}
int vt_iptv_manifest_variant(const VtIptvManifest *m, uint32_t bandwidth, uint32_t width, uint32_t height) {
    if (!m) return -1;
    int selected = -1;
    for (unsigned i = 0; i < m->variant_count; i++) {
        const VtIptvVariant *v = &m->variants[i];
        if (!v->compatible || v->bandwidth > bandwidth || v->width > width || v->height > height) continue;
        if (selected < 0 || v->bandwidth < m->variants[selected].bandwidth) selected = (int)i;
    }
    return selected;
}
