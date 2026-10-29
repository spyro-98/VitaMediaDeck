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
    if (!url || !*url || strlen(url) >= VT_IPTV_RESOURCE_URL_MAX) return 0;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p <= 32 || *p == 127 || *p == '\\') return 0;
    xmlURI *uri = xmlParseURI(url);
    int valid = uri && uri->scheme && uri->server && *uri->server && !uri->user && !uri->fragment &&
        (!strcasecmp(uri->scheme, "https") || !strcasecmp(uri->scheme, "http"));
    xmlFreeURI(uri); return valid;
}
int vt_iptv_input_url(const void *bytes, size_t size, char *out, size_t capacity) {
    if (!out || !capacity) return VT_IPTV_INVALID;
    out[0] = 0;
    if (!bytes || !size) return VT_IPTV_INVALID;
    const unsigned char *first = bytes, *end = first + size;
    if (end[-1] == 0) --end; /* Some generators encode the C terminator. */
    while (first < end && (*first==' ' || *first=='\t' || *first=='\r' || *first=='\n')) ++first;
    if (end-first >= 3 && !memcmp(first, "\xef\xbb\xbf", 3)) first += 3;
    while (first < end && (*first==' ' || *first=='\t' || *first=='\r' || *first=='\n')) ++first;
    while (end > first && (end[-1]==' ' || end[-1]=='\t' || end[-1]=='\r' || end[-1]=='\n')) --end;
    if (first == end || memchr(first, 0, (size_t)(end-first))) return VT_IPTV_INVALID;
    const unsigned char *fragment = memchr(first, '#', (size_t)(end-first));
    if (fragment) end = fragment; /* A fragment is never sent to an HTTP server. */
    size_t n = (size_t)(end-first);
    if (n >= VT_IPTV_URL_MAX || n >= capacity) return VT_IPTV_LIMIT;
    char normalized[VT_IPTV_URL_MAX];
    memcpy(normalized, first, n); normalized[n] = 0;
    if (!safe_url(normalized)) return VT_IPTV_INVALID;
    for (char *p=normalized; *p && *p!=':'; ++p) *p=(char)tolower((unsigned char)*p);
    memcpy(out, normalized, n+1);
    return VT_IPTV_OK;
}
int vt_iptv_resolve_url(const char *base, const char *ref, char *out, size_t capacity) {
    if (!out || !capacity) return VT_IPTV_INVALID;
    out[0] = 0;
    if (!safe_url(base) || !ref || !*ref || strlen(ref) >= VT_IPTV_RESOURCE_URL_MAX) return VT_IPTV_INVALID;
    for (const unsigned char *p = (const unsigned char *)ref; *p; p++)
        if (*p <= 32 || *p == 127 || *p == '\\') return VT_IPTV_INVALID;
    xmlChar *absolute = xmlBuildURI((const xmlChar *)ref, (const xmlChar *)base);
    int result = VT_IPTV_INVALID;
    if (absolute && safe_url((const char *)absolute) &&
        (strncasecmp(base, "https://", 8) || !strncasecmp((const char *)absolute, "https://", 8))) {
        size_t size = strlen((const char *)absolute);
        if (size >= capacity) result = VT_IPTV_LIMIT;
        else {
            memcpy(out, absolute, size + 1);
            /* The HTTP client accepts canonical lower-case scheme names. */
            for (char *p=out; *p && *p!=':'; ++p) *p=(char)tolower((unsigned char)*p);
            result = VT_IPTV_OK;
        }
    }
    xmlFree(absolute); return result;
}
int vt_iptv_text_utf8(const void *input,size_t size,char **out,size_t *length){
 *out=NULL;*length=0;if(!input||!size)return VT_IPTV_INVALID;
 if(size>(20u*1024u*1024u))return VT_IPTV_CATALOG_BODY_LIMIT;
 const unsigned char *p=input;size_t n=0;
 if(size>=2&&((p[0]==255&&p[1]==254)||(p[0]==254&&p[1]==255))){
  int le=p[0]==255;if(size&1)return VT_IPTV_INVALID;
  size_t cap=size>(20u*1024u*1024u)/2?(20u*1024u*1024u)+1:size*2+1;
  char *text=malloc(cap);if(!text)return VT_IPTV_MEMORY;
  for(size_t i=2;i<size;i+=2){
   unsigned cp=le?p[i]|p[i+1]<<8:p[i]<<8|p[i+1];
   if(cp>=0xd800&&cp<=0xdbff){if(i+3>=size){free(text);return VT_IPTV_INVALID;}
    unsigned low=le?p[i+2]|p[i+3]<<8:p[i+2]<<8|p[i+3];if(low<0xdc00||low>0xdfff){free(text);return VT_IPTV_INVALID;}
    cp=0x10000+((cp-0xd800)<<10)+low-0xdc00;i+=2;
   }else if(cp>=0xdc00&&cp<=0xdfff){free(text);return VT_IPTV_INVALID;}
   if(!cp){free(text);return VT_IPTV_INVALID;}
   unsigned need=cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;
   if(n+need>=cap){free(text);return VT_IPTV_CATALOG_BODY_LIMIT;}
   if(need==1)text[n++]=(char)cp;
   else{if(need==4)text[n++]=(char)(0xf0|(cp>>18));
    if(need>=3)text[n++]=(char)((need==3?0xe0:0x80)|((cp>>12)&(need==3?15:63)));
    text[n++]=(char)((need==2?0xc0:0x80)|((cp>>6)&(need==2?31:63)));text[n++]=(char)(0x80|(cp&63));}
  }
  text[n]=0;*out=text;*length=n;return 0;
 }
 if(memchr(input,0,size))return VT_IPTV_INVALID;
 char *text=malloc(size+1);if(!text)return VT_IPTV_MEMORY;memcpy(text,input,size);text[size]=0;
 *out=text;*length=size;return 0;
}
int vt_iptv_local_path(const char *p){return p&&(!strncmp(p,"ux0:",4)||!strncmp(p,"uma0:",5));}
int vt_iptv_resource_resolve(const char *base,const char *ref,char *out,size_t cap){
 if(!base||!ref||!out||!cap)return VT_IPTV_INVALID;
 if(!vt_iptv_local_path(base))return vt_iptv_resolve_url(base,ref,out,cap);
 if(!strncasecmp(ref,"http://",7)||!strncasecmp(ref,"https://",8))return vt_iptv_resolve_url(ref,ref,out,cap);
 char joined[VT_IPTV_URL_MAX],normalized[VT_IPTV_URL_MAX];
 for(const unsigned char *p=(const unsigned char*)ref;*p;p++)if(*p<32||*p==127||*p=='\\')return VT_IPTV_INVALID;
 if(vt_iptv_local_path(ref)){if(strlen(ref)>=sizeof(joined))return VT_IPTV_LIMIT;strcpy(joined,ref);}
 else{
  if(strchr(ref,':')||strlen(base)+strlen(ref)+2>=sizeof(joined))return VT_IPTV_INVALID;
  strcpy(joined,base);char *last=strrchr(joined,'/'),*colon=strchr(joined,':');
  if(ref[0]=='/')colon[1]=0;else if(last&&last>colon)last[1]=0;else colon[1]=0;
  strcat(joined,ref);
 }
 char *colon=strchr(joined,':');size_t used=(size_t)(colon-joined)+1;memcpy(normalized,joined,used);normalized[used]=0;
 const char *part=colon+1;size_t offsets[256];unsigned depth=0;
 while(*part){while(*part=='/')part++;if(!*part)break;const char *end=strchr(part,'/');if(!end)end=part+strlen(part);size_t n=end-part;
  if(n==1&&*part=='.'){}
  else if(n==2&&!memcmp(part,"..",2)){if(!depth)return VT_IPTV_INVALID;used=offsets[--depth];normalized[used]=0;}
  else{if(depth>=256||used+n+2>=sizeof(normalized))return VT_IPTV_LIMIT;offsets[depth++]=used;
   if(depth>1)normalized[used++]='/';memcpy(normalized+used,part,n);used+=n;normalized[used]=0;}
  part=end;
 }
 if(!depth||used>=cap)return VT_IPTV_INVALID;
 memcpy(out,normalized,used+1);return 0;
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
        while(*text==' ' || *text=='\t')text++;
        const char *end = strchr(text, ','); size_t n = end ? (size_t)(end - text) : strlen(text);
        while(n && (text[n-1]==' ' || text[n-1]=='\t'))n--;
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
    /* RFC 8216 recommends CODECS and makes RESOLUTION optional. Missing
     * metadata is not an unsupported codec. The bounded TS demux and existing
     * decoder checks validate the actual stream, without an IPTV-specific resolution cap. */
    out->codecs_declared=has_codecs;
    out->compatible = !has_codecs || codecs_supported(codecs);
    const char *groups[] = {"AUDIO", "VIDEO", "SUBTITLES"};
    for (unsigned i = 0; i < 3; i++) {
        int present = attribute(text, groups[i], group, sizeof(group));
        if (present < 0) return VT_IPTV_INVALID;
        if(present && i<2){out->compatible=0;out->external_av=1;}
        if(present && i==2)out->subtitle_group=1;
        /* Optional sidecar subtitles don't prevent multiplexed A/V playback.
         * Separate audio/video renditions still need a synchronized remuxer. */
    }
    return VT_IPTV_OK;
}
int vt_iptv_manifest_parse_window(const void *bytes, size_t size, const char *base, uint64_t first, VtIptvManifest *out) {
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
            if (pending || out->total_segments || target_seen || seq_seen || disc_seen || disc_tag_seen || out->ended) return VT_IPTV_INVALID;
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
            if (seq_seen++ || out->total_segments || out->variant_count || pending == 2 || !number(line + 22, &sequence)) return VT_IPTV_INVALID;
        } else if (!strncmp(line, "#EXT-X-DISCONTINUITY-SEQUENCE:", 30)) {
            if (disc_seen++ || disc_tag_seen || out->total_segments || out->variant_count || pending == 2 || !number(line + 30, &discontinuity)) return VT_IPTV_INVALID;
        } else if (!strcmp(line, "#EXT-X-DISCONTINUITY")) {
            if (discontinuity == UINT64_MAX || out->variant_count || pending == 2) return VT_IPTV_INVALID;
            discontinuity++; disc_tag_seen = 1;
        } else if (!strcmp(line, "#EXT-X-ENDLIST")) {
            if (pending || out->ended || out->variant_count) return VT_IPTV_INVALID;
            out->ended = 1;
        } else if (!strncmp(line, "#EXT-X-KEY:", 11)) {
            if (strcmp(line + 11, "METHOD=NONE")) return VT_IPTV_ENCRYPTED;
        } else if (!strncmp(line, "#EXT-X-MAP:", 11) || !strncmp(line, "#EXT-X-BYTERANGE:", 17) ||
                   !strncmp(line, "#EXT-X-PART", 11) || !strncmp(line, "#EXT-X-SKIP:", 12) ||
                   !strncmp(line, "#EXT-X-DEFINE:", 14) ||
                   !strcmp(line, "#EXT-X-GAP") || !strcmp(line, "#EXT-X-I-FRAMES-ONLY")) return VT_IPTV_UNSUPPORTED;
        else if(!strncmp(line,"#EXT-X-SESSION-KEY:",19))return VT_IPTV_ENCRYPTED;
        else if (line[0] != '#') {
            if (!pending || out->ended) return VT_IPTV_INVALID;
            if (pending == 2) {
                if (out->variant_count == VT_IPTV_MAX_VARIANTS) return VT_IPTV_LIMIT;
                int result = vt_iptv_resolve_url(base, line, candidate.url, sizeof(candidate.url)); if (result) return result;
                out->variants[out->variant_count++] = candidate;
            } else {
                if (sequence == UINT64_MAX) return VT_IPTV_INVALID;
                VtIptvSegment skipped;
                int keep = sequence >= first && out->segment_count < VT_IPTV_MAX_SEGMENTS;
                VtIptvSegment *segment = keep ? &out->segments[out->segment_count] : &skipped;
                int result = vt_iptv_resolve_url(base, line, segment->url, sizeof(segment->url)); if (result) return result;
                segment->sequence = sequence++; segment->discontinuity = discontinuity; segment->duration_ms = duration;
                if (!target_seen || (duration + 500) / 1000 > out->target_ms / 1000) return VT_IPTV_INVALID;
                if (keep) out->segment_count++;
                out->total_segments++; out->total_duration_ms += duration; out->last_sequence = sequence - 1;
            }
            pending = 0;
        }
    }
    if (!header || pending || (!out->segment_count && !out->variant_count)) return VT_IPTV_INVALID;
    if (!out->ended && out->total_segments > VT_IPTV_MAX_SEGMENTS) return VT_IPTV_LIMIT;
    if (out->segment_count && !target_seen) return VT_IPTV_INVALID;
    for (unsigned i = 0; i < out->segment_count; i++)
        if ((out->segments[i].duration_ms + 500) / 1000 > out->target_ms / 1000) return VT_IPTV_INVALID;
    return VT_IPTV_OK;
}
int vt_iptv_manifest_parse(const void *bytes, size_t size, const char *base, VtIptvManifest *out) {
    int result = vt_iptv_manifest_parse_window(bytes, size, base, 0, out);
    return !result && out->total_segments > VT_IPTV_MAX_SEGMENTS ? VT_IPTV_LIMIT : result;
}
int vt_iptv_manifest_variant(const VtIptvManifest *m, uint32_t bandwidth, uint32_t width, uint32_t height) {
    if (!m) return -1;
    int selected = -1;
    for (unsigned i = 0; i < m->variant_count; i++) {
        const VtIptvVariant *v = &m->variants[i];
        if (!v->compatible || (bandwidth&&v->bandwidth > bandwidth) || (width&&v->width > width) || (height&&v->height > height)) continue;
        if (selected < 0 || v->bandwidth < m->variants[selected].bandwidth) selected = (int)i;
    }
    return selected;
}
