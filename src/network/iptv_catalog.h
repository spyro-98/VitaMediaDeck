#ifndef VMD_IPTV_CATALOG_H
#define VMD_IPTV_CATALOG_H
#include "network/iptv_manifest.h"
/* ~1.7 MiB of bounded catalogue metadata; artwork is still loaded on demand. */
#define VT_IPTV_CHANNEL_MAX 512
#define VT_IPTV_SAVED_MAX 128
enum { VT_IPTV_HLS, VT_IPTV_FILE, VT_IPTV_LIST, VT_IPTV_DASH };
typedef struct {
    char url[VT_IPTV_URL_MAX], title[160], logo[512], artwork_path[192];
    char user_agent[VT_IPTV_USER_AGENT_MAX];
    char group[128], channel_id[128];
    int mode, starred;
    int availability; /* Session-only result; never serialized as permanent truth. */
} VtIptvChannel;
typedef struct {
    VtIptvChannel items[VT_IPTV_CHANNEL_MAX];
    unsigned count;
    int truncated;
} VtIptvCatalog;
int vt_iptv_link_kind(const char *url);
int vt_iptv_user_agent_valid(const char *text);
/* Case/spacing/punctuation-insensitive local search, never URL query tokens. */
int vt_iptv_channel_matches(const VtIptvChannel *item,const char *query);
int vt_iptv_catalog_parse(const void *data, size_t size, const char *base, VtIptvCatalog *out);
int vt_iptv_saved_load(VtIptvCatalog *out, int starred_only);
int vt_iptv_saved_set(const VtIptvChannel *item);
/* Remove only this saved URL, never remote content or other saved channels. */
int vt_iptv_saved_remove(const char *url);
#endif
