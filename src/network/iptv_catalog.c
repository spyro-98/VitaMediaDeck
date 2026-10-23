#include "network/iptv_catalog.h"
#include "network/iptv_trace.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
int vt_iptv_user_agent_valid(const char *text) {
    if(!text || strlen(text)>=VT_IPTV_USER_AGENT_MAX)return 0;
    for(const unsigned char *p=(const unsigned char*)text;*p;p++)if(*p<32 || *p>126)return 0;
    return 1;
}
static void search_key(const char *text,char *out,size_t capacity) {
    size_t used=0;
    for(const unsigned char *p=(const unsigned char *)text;*p && used+1<capacity;p++) {
        unsigned c=*p;
        if(c<128){if(isalnum(c))out[used++]=(char)tolower(c);}
        else if(c==0xc3 && p[1]>=0x80 && p[1]<=0xbf) {
            /* Fold common Latin accents as well as upper/lower case. */
            unsigned d=*++p;
            if(d>=0x80 && d<=0x9e && d!=0x97)d+=32;
            char folded=d>=0xa0&&d<=0xa5?'a':d==0xa7?'c':d>=0xa8&&d<=0xab?'e':
                d>=0xac&&d<=0xaf?'i':d==0xb1?'n':d>=0xb2&&d<=0xb6?'o':
                d>=0xb9&&d<=0xbc?'u':d==0xbd||d==0xbf?'y':0;
            if(folded)out[used++]=folded;
            else if(used+2<capacity){out[used++]=(char)c;out[used++]=(char)d;}
        } else out[used++]=(char)c;
    }
    out[used]=0;
}
int vt_iptv_channel_matches(const VtIptvChannel *item,const char *query) {
    if(!item)return 0;
    char needle[256],key[VT_IPTV_URL_MAX];search_key(query?query:"",needle,sizeof(needle));
    if(!needle[0])return 1;
    const char *fields[]={item->title,item->group,item->channel_id};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++) {
        search_key(fields[i],key,sizeof(key));if(strstr(key,needle))return 1;
    }
    char public_url[VT_IPTV_URL_MAX];size_t n=strcspn(item->url,"?#");
    if(n>=sizeof(public_url))n=sizeof(public_url)-1;
    memcpy(public_url,item->url,n);public_url[n]=0;
    search_key(public_url,key,sizeof(key));return strstr(key,needle)!=NULL;
}
int vt_iptv_link_kind(const char *url) {
    const char *end = strchr(url, '?'); if (!end) end = url + strlen(url);
    const char *dot = end;
    while (dot > url && dot[-1] != '/' && dot[-1] != '.') dot--;
    if (dot == url || dot[-1] != '.') return VT_IPTV_HLS;
    size_t n = end - dot;
    if (n == 3 && !strncasecmp(dot, "m3u", n)) return VT_IPTV_LIST;
    if (n == 3 && !strncasecmp(dot, "mpd", n)) return VT_IPTV_DASH;
    const char *files[] = {"mp4", "m4v", "mkv", "mov", "webm", "avi", "ts"};
    for (unsigned i = 0; i < sizeof(files)/sizeof(*files); i++)
        if (n == strlen(files[i]) && !strncasecmp(dot, files[i], n)) return VT_IPTV_FILE;
    return VT_IPTV_HLS;
}
static void metadata(char *text, VtIptvChannel *item, const char *base) {
    /* Only commas outside quotes delimit the title. Executable options,
     * arbitrary headers, cookies and key directives are never imported. */
    int quoted = 0; char *comma = NULL;
    for (char *p = text; *p; p++) { if (*p == '"') quoted = !quoted; else if (*p == ',' && !quoted) { comma = p; break; } }
    if (comma) { *comma++ = 0; snprintf(item->title, sizeof(item->title), "%s", comma); }
    char *p = text;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        char *key = p; while (*p && *p != '=' && !isspace((unsigned char)*p)) p++;
        if (*p != '=') continue;
        size_t length = p++ - key;
        int quote = *p == '"'; if (quote) p++;
        char *value = p; while (*p && (quote ? *p != '"' : !isspace((unsigned char)*p))) p++;
        char saved = *p; *p = 0;
        if (length == 8 && !memcmp(key, "tvg-logo", 8))
            vt_iptv_resolve_url(base, value, item->logo, sizeof(item->logo));
        if (!item->title[0] && length == 8 && !memcmp(key, "tvg-name", 8))
            snprintf(item->title, sizeof(item->title), "%s", value);
        if(length==11 && !memcmp(key,"group-title",11))snprintf(item->group,sizeof(item->group),"%s",value);
        if(length==6 && !memcmp(key,"tvg-id",6))snprintf(item->channel_id,sizeof(item->channel_id),"%s",value);
        *p = saved; if (*p) p++;
    }
}
int vt_iptv_catalog_parse(const void *bytes, size_t size, const char *base, VtIptvCatalog *out) {
    if (!out) return VT_IPTV_INVALID;
    memset(out, 0, sizeof(*out));
    if (!bytes || !size || size > VT_IPTV_PLAYLIST_LIMIT || memchr(bytes, 0, size)) return VT_IPTV_LIMIT;
    const char *data = bytes; size_t offset = 0; int header = 0;
    /* Channel lists (unlike strict HLS manifests) commonly have a UTF-8 BOM,
     * extended header attributes and whitespace from hand-edited exports. */
    if(size>=3 && !memcmp(data,"\xef\xbb\xbf",3))offset=3;
    VtIptvChannel pending = {0};
    while (offset < size) {
        size_t start = offset; while (offset < size && data[offset] != '\n') offset++;
        size_t n = offset - start; if (offset < size) offset++;
        while(n && isspace((unsigned char)data[start+n-1]))n--;
        while(n && (data[start]==' ' || data[start]=='\t')){start++;n--;}
        if (!n) continue;
        if (n >= 4096) return VT_IPTV_LIMIT;
        char line[4096]; memcpy(line, data+start, n); line[n] = 0;
        if (!header) {
            if(strncmp(line,"#EXTM3U",7) || (line[7] && !isspace((unsigned char)line[7])))return VT_IPTV_INVALID;
            header=1;continue;
        }
        if (!strncmp(line, "#EXT-X-", 7)) return VT_IPTV_UNSUPPORTED;
        if (!strncmp(line, "#EXTINF:", 8)) { memset(&pending, 0, sizeof(pending)); metadata(line+8, &pending, base); }
        else if (!strncmp(line,"#EXTVLCOPT:http-user-agent=",27)) {
            /* This one declarative player identifier is required by public
             * broadcasters. Bound it and reject control/header injection. */
            if(!vt_iptv_user_agent_valid(line+27)) {
                IPTV_TRACE("IPTV catalog rejected: user-agent length=%u limit=%u\n",
                    (unsigned)strlen(line+27),(unsigned)VT_IPTV_USER_AGENT_MAX-1);
                return VT_IPTV_INVALID;
            }
            snprintf(pending.user_agent,sizeof(pending.user_agent),"%s",line+27);
        } else if (line[0] != '#') {
            /* An explicit HTTP channel in an HTTPS-hosted directory is a
             * separate resource, not a redirect downgrade of the playlist.
             * Relative URLs and all HLS redirects retain strict resolution. */
            const char *origin=!strncasecmp(line,"http://",7) || !strncasecmp(line,"https://",8)?line:base;
            if (!vt_iptv_resolve_url(origin, line, pending.url, sizeof(pending.url))) {
                pending.mode = vt_iptv_link_kind(pending.url);
                if (!pending.title[0]) snprintf(pending.title, sizeof(pending.title), "IPTV %u", out->count+1);
                if (out->count < VT_IPTV_CHANNEL_MAX) out->items[out->count++] = pending;
                else out->truncated = 1;
            }
            memset(&pending, 0, sizeof(pending));
        }
    }
    return out->count ? 0 : VT_IPTV_INVALID;
}
