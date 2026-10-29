#ifndef VMD_IPTV_CATALOG_H
#define VMD_IPTV_CATALOG_H
#include "network/iptv_manifest.h"
#include "network/iptv_options.h"
/* ~1.7 MiB of bounded catalogue metadata; artwork is still loaded on demand. */
#define VT_IPTV_CHANNEL_MAX 512
#define VT_IPTV_SAVED_MAX 128
/* Channel directories are larger than playback manifests; keep limits separate. */
#define VT_IPTV_CATALOG_LIMIT (20u * 1024u * 1024u)
enum { VT_IPTV_HLS, VT_IPTV_FILE, VT_IPTV_LIST, VT_IPTV_DASH };
enum {VT_IPTV_NAME_UNKNOWN,VT_IPTV_NAME_AUTO,VT_IPTV_NAME_METADATA,VT_IPTV_NAME_CUSTOM};
typedef struct {
    char url[VT_IPTV_URL_MAX], title[160], logo[512], artwork_path[192];
    char user_agent[VT_IPTV_USER_AGENT_MAX];
    char group[128], channel_id[128], guide_url[512], guide_name[160];
    int guide_shift;
    VtIptvPreference preferred;
    int mode, starred;
    int name_source, single_channel; /* Optional saved metadata; old records remain valid. */
    int availability; /* Session-only result; never serialized as permanent truth. */
} VtIptvChannel;
static inline int vt_iptv_channel_collection(const VtIptvChannel *v){return v->mode==VT_IPTV_LIST&&!v->single_channel;}
typedef struct {
    VtIptvChannel items[VT_IPTV_CHANNEL_MAX];
    unsigned count;
    int truncated;
} VtIptvCatalog;
int vt_iptv_link_kind(const char *url);
int vt_iptv_user_agent_valid(const char *text);
/* Case/spacing/punctuation-insensitive local search, never URL query tokens. */
int vt_iptv_channel_compare_name(const VtIptvChannel *a,const VtIptvChannel *b);
int vt_iptv_channel_matches(const VtIptvChannel *item,const char *query);
void vt_iptv_catalog_guide_reference(const char *base,const char *value,char out[512]);
int vt_iptv_catalog_provider_guide(const char *playlist,char out[512]);
int vt_iptv_catalog_parse(const void *data, size_t size, const char *base, VtIptvCatalog *out);
/* Classifies the actual document, independent of the resource's extension. */
int vt_iptv_channel_generic_name(const VtIptvChannel *item);
/* Merge only advertised identity; never substitutes programme/segment titles. */
int vt_iptv_channel_merge(VtIptvChannel *item,const VtIptvChannel *metadata);
int vt_iptv_catalog_metadata(const void *data,size_t size,const char *base,VtIptvChannel *item);
int vt_iptv_catalog_content_kind(const void *data,size_t size);
/* Static landing pages: declarative meta refresh or an unambiguous M3U anchor.
 * No script execution; URL policy remains the shared resolver policy.
 * Returns 0 with a target, 1 when no supported redirect is present. */
int vt_iptv_catalog_redirect(const void *data,size_t size,const char *base,char *out,size_t capacity);
unsigned vt_iptv_saved_revision(void);
int vt_iptv_saved_load(VtIptvCatalog *out, int starred_only);
int vt_iptv_saved_set(const VtIptvChannel *item);
/* Remove only this saved URL, never remote content or other saved channels. */
int vt_iptv_saved_remove(const char *url);
#endif
