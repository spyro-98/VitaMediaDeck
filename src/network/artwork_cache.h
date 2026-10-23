#ifndef VMD_ARTWORK_CACHE_H
#define VMD_ARTWORK_CACHE_H
#include <stddef.h>
/* Shared persistent raster cache for shows, episodes, stations and IPTV logos.
 * Worker-only. URL digest names never persist query tokens. */
typedef char *(*VtArtworkFetch)(const char *, volatile int *, size_t *);
int vt_artwork_cache_get(const char *url, char *path, size_t capacity,
                        volatile int *cancel, VtArtworkFetch fetch);
#endif
