#ifndef VMD_ONLINE_AUDIO_H
#define VMD_ONLINE_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define VT_ONLINE_MAX 64
#define VT_ONLINE_CACHE_MAX 256
#define VT_ONLINE_FEED_LIMIT (8u * 1024u * 1024u)
enum { VT_ONLINE_RADIO = 1, VT_ONLINE_FEED, VT_ONLINE_EPISODE, VT_ONLINE_COUNTRY,
       VT_ONLINE_TAG, VT_ONLINE_SHOW, VT_ONLINE_MENU };
typedef struct {
	char title[160];
	char url[512];
	char id[192];
	char detail[160];
	char author[160];
	char description[1024];
	char artwork_url[512];
	char artwork_path[192];
	char show_title[160]; /* RSS channel title, including direct-feed navigation. */
	uint64_t duration_ms;
	char published_date[11]; /* Feed-local calendar date, YYYY-MM-DD; empty if unknown. */
	int kind;
} VtOnlineItem;
/* Saved countries are local navigation shortcuts, never stream endpoints. */
static inline int vt_online_item_country(const VtOnlineItem *v) {
 return v && v->id[0]>='A' && v->id[0]<='Z' && v->id[1]>='A' && v->id[1]<='Z' && !v->id[2] &&
  (v->kind==VT_ONLINE_COUNTRY ||
   ((v->kind==VT_ONLINE_RADIO || v->kind==VT_ONLINE_FEED) &&
    !strncmp(v->url,"vmd-country:",12) && !strcmp(v->url+12,v->id)));
}
typedef struct {
	VtOnlineItem items[VT_ONLINE_CACHE_MAX];
	int count;
	char query[512];
	int kind;
	volatile int cancel;
	volatile int *cancel_parent;
	char country[3]; /* Empty = worldwide radio / US podcast storefront. */
	unsigned offset;
	int has_more;
	unsigned batch_size;
	int search_field;
	int browse; /* Catalogue mode: popularity, not free-text search. */
} VtOnlineQuery;
int vt_online_url_valid(const char *url);
int vt_online_fetch_query(void *query);
/* Read-only snapshots are shown immediately; successful worker fetches replace them. */
int vt_online_query_cache_read(VtOnlineQuery *query);
int vt_online_query_cache_write(const VtOnlineQuery *query);
int vt_online_parse_feed(const char *data, size_t size, const char *url, VtOnlineQuery *out);
unsigned vt_online_saved_revision(void);
int vt_online_load_saved(VtOnlineItem *out, int capacity);
int vt_online_save(const VtOnlineItem *item);
/* Exact kind + URL; removes only the local favourite, preserving other media. */
int vt_online_remove(int kind, const char *url);
int vt_online_resolve_show(VtOnlineItem *item, volatile int *cancel);
/* Bounded raster cover cache; call on a worker, not during drawing. */
int vt_online_fetch_cover(VtOnlineItem *item, volatile int *cancel);
void vt_online_search_normalize(const char *in, char *spaced, char *compact, size_t capacity);
#endif
