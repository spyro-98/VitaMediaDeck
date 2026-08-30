#ifndef VITAMEDIADECK_MEDIA_MUSIC_METADATA_H
#define VITAMEDIADECK_MEDIA_MUSIC_METADATA_H

#include <stdint.h>

typedef struct {
	char title[200];
	char artist[96];
	char album[128];
	char artwork_path[512];
	uint64_t duration_ms;
} VtMusicMetadata;

int vt_music_metadata_save(const char *media_path, const VtMusicMetadata *metadata);
int vt_music_metadata_load(const char *media_path, VtMusicMetadata *metadata);

#endif
