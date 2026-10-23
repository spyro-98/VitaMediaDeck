#ifndef VMD_PODCAST_METADATA_H
#define VMD_PODCAST_METADATA_H
#include "media/music_metadata.h"
typedef struct {
    char title[160], author[160], show[160], description[1024];
    uint64_t duration_ms;
    char published_date[11];
} VtPodcastMetadata;
/* Hidden siblings of the final audio filename, independent of cache eviction.
 * Returns 1 when metadata was saved but a requested cover could not be copied. */
int vt_podcast_metadata_save(const char *media, const VtPodcastMetadata *metadata,
                             const char *cover, volatile int *cancel);
int vt_podcast_metadata_load(const char *media, VtMusicMetadata *metadata);
/* Do not reuse a filename with orphaned companions from a manual deletion. */
int vt_podcast_destination_available(const char *media);
/* Move only app-owned companions. On failure, roll back completed moves. */
int vt_podcast_companions_move(const char *from, const char *to);
int vt_podcast_companions_remove(const char *media);
#endif
