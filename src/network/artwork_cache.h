#ifndef VMD_ARTWORK_CACHE_H
#define VMD_ARTWORK_CACHE_H
#include <stddef.h>
/* Shared persistent raster cache for shows, episodes, stations and IPTV logos.
 * Worker-only. URL digest names never persist query tokens. */
/* Init discovers existing files on an independent background worker. Get uses
 * a keyed disk reference without awaiting that scan; peek/due remain memory-only. */
void vt_artwork_cache_init(void);
int vt_artwork_cache_peek(const char *url,char *path,size_t capacity);
/* Resolve persisted paths to the latest downloaded revision, without I/O. */
int vt_artwork_cache_current(const char *old_path,char *path,size_t capacity);
int vt_artwork_cache_due(const char *url);
typedef char *(*VtArtworkFetch)(const char *, volatile int *, size_t *);
/* Explicit refresh bypasses age/backoff, preserving the last valid image. */
int vt_artwork_cache_refresh(const char *url, char *path, size_t capacity,
                            volatile int *cancel, VtArtworkFetch fetch);
int vt_artwork_cache_get(const char *url, char *path, size_t capacity,
                        volatile int *cancel, VtArtworkFetch fetch);
#endif
