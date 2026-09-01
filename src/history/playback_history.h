#ifndef VITAMEDIADECK_HISTORY_PLAYBACK_HISTORY_H
#define VITAMEDIADECK_HISTORY_PLAYBACK_HISTORY_H

#include <stdint.h>

/* Persistent resume points keyed separately for local and remote media. */
int vt_playback_history_init(void);
uint64_t vt_playback_history_position(const char *video_id,
	                                  uint64_t duration_ms);
int vt_playback_history_get(const char *video_id, uint64_t *position_ms,
	                        uint64_t *duration_ms);
/* Raw watched progress, including completed videos. Unlike get()/position(),
 * this never applies the resume-end guard. */
int vt_playback_history_progress(const char *video_id, uint64_t *position_ms,
	                             uint64_t *duration_ms);
int vt_playback_history_update(const char *video_id, uint64_t position_ms,
	                           uint64_t duration_ms);

/* Stable local-file key shared by playback and library cells. */
void vt_playback_history_local_id(const char *path, char out[16]);

#endif
