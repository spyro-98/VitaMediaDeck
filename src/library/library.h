#ifndef VMD_LIBRARY_H
#define VMD_LIBRARY_H
#include <stdint.h>
#define VT_LIBRARY_MAX 512
#define VT_LIBRARY_RESULTS 192
enum {
    VT_LIBRARY_LOCAL,
    VT_LIBRARY_JELLYFIN,
    VT_LIBRARY_NAVIDROME,
    VT_LIBRARY_IMMICH,
    VT_LIBRARY_IPTV,
    VT_LIBRARY_RADIO,
    VT_LIBRARY_PODCAST,
    VT_LIBRARY_NETWORK,
    VT_LIBRARY_PROVIDER_COUNT
};
enum {
    VT_LIBRARY_VIDEO = 1,
    VT_LIBRARY_AUDIO,
    VT_LIBRARY_IMAGE,
    VT_LIBRARY_LIVE,
    VT_LIBRARY_FOLDER,
    VT_LIBRARY_FEED
};
enum { VT_LIBRARY_ALL, VT_LIBRARY_CONTINUE, VT_LIBRARY_RECENT };
typedef struct {
    char id[16], history_id[64], server[16];
    char server_name[64];
    char user_agent[256], artwork_url[512];
    char locator[2048], context[2048], title[256], artist[160], album[160], artwork[512];
    uint64_t duration_ms, size;
    int provider, kind, mode;
} VtLibraryItem;
typedef struct {
    VtLibraryItem item;
    uint64_t position_ms, duration_ms, order;
    int started, cached;
} VtLibraryResult;
typedef int (*VtLibrarySample)(char id[64], uint64_t *position, uint64_t *duration);
int vt_library_init(VtLibrarySample sample);
void vt_library_shutdown(void);
void vt_library_identity(VtLibraryItem *item);
void vt_library_put(const VtLibraryItem *item);
void vt_library_bind(const VtLibraryItem *);
void vt_library_started(const VtLibraryItem *item);
void vt_library_progress(const char *history_id, uint64_t position, uint64_t duration);
void vt_library_import(const VtLibraryItem *, uint64_t position, uint64_t duration, uint64_t order);
uint64_t vt_library_resume_fallback(const VtLibraryItem *, uint64_t legacy);
void vt_library_start_id(const char *history_id);
uint64_t vt_library_resume(const VtLibraryItem *item);
int vt_library_query(const char *query, int view, VtLibraryResult *out, int capacity);
int vt_library_matches(const VtLibraryItem *item, const char *query);
const char *vt_library_provider_name(int provider);
/* Request only: checkpoint I/O belongs to the low priority worker. */
/* Worker-only bounded selection: provider quotas / folder scope before copying. */
int vt_library_query_search(const char *query, const VtLibraryItem *parent, VtLibraryResult *out,
                            int capacity);
void vt_library_checkpoint(void);
unsigned vt_library_revision(void);
#endif
