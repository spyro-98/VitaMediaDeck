#ifndef VITATUBE_HISTORY_PLAYBACK_HISTORY_H
#define VITATUBE_HISTORY_PLAYBACK_HISTORY_H

#include <stdint.h>

/* Persistent resume points for local media. */
int vt_playback_history_init(void);
uint64_t vt_playback_history_position(const char *video_id,
	                                  uint64_t duration_ms);
int vt_playback_history_get(const char *video_id, uint64_t *position_ms,
	                        uint64_t *duration_ms);
int vt_playback_history_update(const char *video_id, uint64_t position_ms,
	                           uint64_t duration_ms);

#endif
