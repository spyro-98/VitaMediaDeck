#ifndef VITAWAVE_MEDIA_MUSIC_METADATA_H
#define VITAWAVE_MEDIA_MUSIC_METADATA_H

#include <stdint.h>

typedef struct {
	char title[200];
	char artist[96];
	char album[128];
	uint64_t duration_ms;
} VtMusicMetadata;

int vt_music_metadata_save(const char *mp3_path, const VtMusicMetadata *metadata);
int vt_music_metadata_load(const char *mp3_path, VtMusicMetadata *metadata);

#endif
