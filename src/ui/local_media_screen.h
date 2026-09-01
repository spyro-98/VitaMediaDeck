#ifndef VITAMEDIADECK_UI_LOCAL_MEDIA_SCREEN_H
#define VITAMEDIADECK_UI_LOCAL_MEDIA_SCREEN_H

#include <stdint.h>

#define VT_LOCAL_MEDIA_PATH_MAX 512
#define VT_LOCAL_MEDIA_NAME_MAX 192
#define VT_LOCAL_MEDIA_ARTIST_MAX 160

typedef enum {
	VT_LOCAL_MEDIA_VIDEO = 1,
	VT_LOCAL_MEDIA_AUDIO = 2,
	VT_LOCAL_MEDIA_IMAGE = 3
} VtLocalMediaType;

typedef enum {
	VT_LOCAL_MEDIA_SOURCE_FILE = 0
} VtLocalMediaSource;

typedef struct {
	char path[VT_LOCAL_MEDIA_PATH_MAX];
	char name[VT_LOCAL_MEDIA_NAME_MAX];
	char artwork_path[VT_LOCAL_MEDIA_PATH_MAX];
	char artist[VT_LOCAL_MEDIA_ARTIST_MAX];
	char album[128];
	VtLocalMediaType type;
	VtLocalMediaSource source;
	uint64_t size;
	uint64_t duration_ms;
} VtLocalMediaItem;

#define UI_LOCAL_MEDIA_ACTION_BACK 0
#define UI_LOCAL_MEDIA_ACTION_PLAY 1
#define UI_LOCAL_MEDIA_ACTION_RENAME 3
#define UI_LOCAL_MEDIA_ACTION_DELETE 4
#define UI_LOCAL_MEDIA_ACTION_BROWSE_FILES 5
#define UI_LOCAL_MEDIA_ACTION_SECTION_BASE 10

int ui_local_media_screen(VtLocalMediaItem *selected_out);

/* Uses the most recently scanned local catalog to advance the music queue.
 * direction is normally +1; random ignores ordering but never returns the
 * current item when another track exists. */
int ui_local_media_next_audio(const char *current_path, int direction,
	                          int random, VtLocalMediaItem *out);

#endif
