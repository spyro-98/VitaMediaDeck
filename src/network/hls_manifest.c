#include "network/hls_source.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 8192
#define EXPANDED_LIMIT (2u * 1024u * 1024u)
typedef struct { char *text; size_t size, capacity; } Text;
static int append(Text *out, const char *text, size_t length) {
    if (length > EXPANDED_LIMIT - out->size) return VT_IPTV_LIMIT;
    if (out->size + length + 1 > out->capacity) {
        size_t capacity = out->capacity ? out->capacity * 2 : 4096;
        if (capacity < out->size + length + 1) capacity = out->size + length + 1;
        char *p = realloc(out->text, capacity);
        if (!p) return VT_IPTV_MEMORY;
        out->text = p; out->capacity = capacity;
    }
    memcpy(out->text + out->size, text, length); out->size += length;
    out->text[out->size] = 0; return 0;
}
/* Attribute boundaries matter: URI must not match SERVER-URI or KEYFORMAT.
 * Commas inside quoted CODECS/NAME values are not separators. */
static int attribute(const char *line, const char *name, char *out, size_t cap) {
    const char *p = strchr(line, ':'); if (!p) return 0; ++p;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
        const char *key = p;
        while (*p && *p != '=' && *p != ',') ++p;
        if (*p != '=') { if (*p) ++p; continue; }
        size_t keylen = (size_t)(p++ - key);
        const char *value; size_t length;
        if (*p == '"') { value = ++p; while (*p && *p != '"') ++p;
            if (!*p) return -1;
            length = (size_t)(p++ - value);
        } else { value = p; while (*p && *p != ',') ++p; length = (size_t)(p - value); }
        if (strlen(name) == keylen && !memcmp(key, name, keylen)) {
            if (length >= cap) return -1;
            memcpy(out, value, length); out[length] = 0; return 1;
        }
    }
    return 0;
}
static int rewrite(Text *out, const char *line, const char *base) {
    char resolved[VT_IPTV_URL_MAX];
    if (*line != '#') {
        int r = vt_iptv_resolve_url(base, line, resolved, sizeof(resolved));
        return r ? r : append(out, resolved, strlen(resolved));
    }
    /* HLS URI attributes are quoted strings. Resolve against the response's
     * final URL before FFmpeg sees it, including key/map/rendition resources. */
    const char *uri = strstr(line, "URI=\"");
    if (!uri || (uri > line && uri[-1] != ',' && uri[-1] != ':'))
        return append(out, line, strlen(line));
    const char *begin = uri + 5, *end = strchr(begin, '"');
    if (!end || end == begin || (size_t)(end - begin) >= sizeof(resolved)) return VT_IPTV_INVALID;
    char reference[VT_IPTV_URL_MAX]; memcpy(reference, begin, (size_t)(end - begin)); reference[end-begin] = 0;
    int r = vt_iptv_resolve_url(base, reference, resolved, sizeof(resolved));
    if (!r) r = append(out, line, (size_t)(begin-line));
    if (!r) r = append(out, resolved, strlen(resolved));
    if (!r) r = append(out, end, strlen(end));
    return r;
}
int vt_hls_prepare_manifest(const void *data, size_t size, const char *base,
    const VtIptvSourceConfig *config, unsigned char **output, size_t *length) {
    if (!output || !length) return VT_IPTV_INVALID;
    *output = NULL; *length = 0;
    if (!data || !size || size > VT_IPTV_PLAYLIST_LIMIT || !config || memchr(data, 0, size)) return VT_IPTV_INVALID;
    char *copy = malloc(size+1); char **lines = calloc(MAX_LINES, sizeof(*lines));
    if (!copy || !lines) { free(copy); free(lines); return VT_IPTV_MEMORY; }
    memcpy(copy, data, size); copy[size] = 0;
    char *p = copy; if (size >= 3 && !memcmp(p, "\xef\xbb\xbf", 3)) p += 3;
    unsigned count = 0, variants = 0, media = 0; int result = 0;
    while (*p) {
        if (count == MAX_LINES) { result = VT_IPTV_LIMIT; goto done; }
        char *line = p; p += strcspn(p, "\r\n"); if (*p) *p++ = 0;
        while (*p == '\r' || *p == '\n') ++p;
        while (*line == ' ' || *line == '\t') ++line;
        size_t n = strlen(line); while (n && isspace((unsigned char)line[n-1])) line[--n] = 0;
        if (*line) lines[count++] = line;
    }
    if (!count || strcmp(lines[0], "#EXTM3U")) { result = VT_IPTV_INVALID; goto done; }
    int selected = -1, best_video = -1; unsigned long best = ~0ul;
    char audio_group[256] = "", video_group[256] = "";
    for (unsigned i = 1; i < count; ++i) {
        const char *line = lines[i];
        if (!strncmp(line, "#EXT-X-MEDIA:", 13) && ++media > 64) { result = VT_IPTV_LIMIT; goto done; }
        if (strncmp(line, "#EXT-X-STREAM-INF:", 18)) continue;
        if (++variants > 64 || i+1 >= count || *lines[i+1] == '#') { result = VT_IPTV_INVALID; goto done; }
        char value[512]; unsigned long bandwidth = 0; unsigned width = 0, height = 0;
        int r = attribute(line, "BANDWIDTH", value, sizeof(value));
        if (r < 0) { result = VT_IPTV_INVALID; goto done; }
        if (r) { char *end; bandwidth = strtoul(value, &end, 10); if (*end) continue; }
        r = attribute(line, "RESOLUTION", value, sizeof(value));
        if (r < 0 || (r && sscanf(value, "%ux%u", &width, &height) != 2)) continue;
        if ((config->bandwidth_limit && bandwidth > config->bandwidth_limit) ||
            (config->width_limit && width > config->width_limit) ||
            (config->height_limit && height > config->height_limit)) continue;
        r = attribute(line, "CODECS", value, sizeof(value));
        if (r < 0) continue;
        /* Do not choose HEVC/AV1 over an available H.264 rendition. Missing
         * codec hints are legal; actual streams are checked after probing. */
        if (r && (strstr(value,"hvc1") || strstr(value,"hev1") || strstr(value,"av01") || strstr(value,"vp09")) && !strstr(value,"avc")) continue;
        int video = width || height || (r && strstr(value,"avc")) ? 2 : r ? 0 : 1;
        if (selected < 0 || video > best_video || (video == best_video && bandwidth < best)) {
            selected = (int)i; best = bandwidth; best_video = video;
        }
    }
    if (variants && selected < 0) { result = VT_IPTV_VARIANT_LIMIT; goto done; }
    if (selected >= 0 && (attribute(lines[selected], "AUDIO", audio_group, sizeof(audio_group)) < 0 ||
        attribute(lines[selected], "VIDEO", video_group, sizeof(video_group)) < 0)) { result = VT_IPTV_INVALID; goto done; }
    int rendition[2] = {-1,-1}, score[2] = {-1,-1};
    for (unsigned i = 1; i < count; ++i) {
        if (strncmp(lines[i], "#EXT-X-MEDIA:", 13)) continue;
        char type[32], group[256], value[32];
        if (attribute(lines[i], "TYPE", type, sizeof(type)) != 1 ||
            attribute(lines[i], "GROUP-ID", group, sizeof(group)) != 1) continue;
        int index = !strcmp(type,"AUDIO") ? 0 : !strcmp(type,"VIDEO") ? 1 : -1;
        if (index < 0 || strcmp(group, index ? video_group : audio_group)) continue;
        int rank = 0;
        if (attribute(lines[i],"AUTOSELECT",value,sizeof(value)) == 1 && !strcmp(value,"YES")) rank = 1;
        if (attribute(lines[i],"DEFAULT",value,sizeof(value)) == 1 && !strcmp(value,"YES")) rank = 2;
        if (rank > score[index]) { score[index] = rank; rendition[index] = (int)i; }
    }
    Text out = {0};
    for (unsigned i = 0; i < count && !result; ++i) {
        const char *line = lines[i];
        if (!strncmp(line,"#EXT-X-STREAM-INF:",18) && (int)i != selected) { ++i; continue; }
        if (!strncmp(line,"#EXT-X-MEDIA:",13) && (int)i != rendition[0] && (int)i != rendition[1]) continue;
        if (!strncmp(line,"#EXT-X-I-FRAME-STREAM-INF:",26) || !strncmp(line,"#EXT-X-SESSION-DATA:",20)) continue;
        if (!strncmp(line,"#EXT-X-KEY:",11) || !strncmp(line,"#EXT-X-SESSION-KEY:",19)) {
            char method[64], format[128];
            int m = attribute(line,"METHOD",method,sizeof(method)), k = attribute(line,"KEYFORMAT",format,sizeof(format));
            if (m != 1 || k < 0) { result = VT_IPTV_INVALID; break; }
            if ((k && strcmp(format,"identity")) ||
                (strcmp(method,"NONE") && strcmp(method,"AES-128") && strcmp(method,"SAMPLE-AES"))) {
                result = VT_IPTV_ENCRYPTED; break;
            }
        }
        result = rewrite(&out, line, base);
        if (!result) result = append(&out, "\n", 1);
    }
    if (!result) { *output = (unsigned char *)out.text; *length = out.size; }
    else free(out.text);
done:
    free(lines); free(copy); return result;
}
