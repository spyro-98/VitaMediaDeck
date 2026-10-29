#ifndef VMD_ARTWORK_CACHE_H
#define VMD_ARTWORK_CACHE_H
#include <stddef.h>
/* Shared persistent raster cache for shows, episodes, stations and IPTV logos.
 * Worker-only. URL digest names never persist query tokens. */
/* Init scans existing files on the startup/background worker. Peek/due are
 * nonblocking memory-only calls; get refreshes stale images on a worker. */
void vt_artwork_cache_init(void);
int vt_artwork_cache_peek(const char *url,char *path,size_t capacity);
/* Resolve persisted paths to the latest downloaded revision, without I/O. */
int vt_artwork_cache_current(const char *old_path,char *path,size_t capacity);
int vt_artwork_cache_due(const char *url);
typedef char *(*VtArtworkFetch)(const char *, volatile int *, size_t *);
int vt_artwork_cache_get(const char *url, char *path, size_t capacity,
                        volatile int *cancel, VtArtworkFetch fetch);
#endif
