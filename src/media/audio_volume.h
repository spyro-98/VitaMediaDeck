#ifndef VITAMEDIADECK_MEDIA_AUDIO_VOLUME_H
#define VITAMEDIADECK_MEDIA_AUDIO_VOLUME_H
#include "media/audio_gain.h"
#define VT_AUDIO_VOLUME_MAX_PERCENT VITA_AUDIO_VOLUME_MAX_PERCENT

/* Shared 0..150% volume: 100% is unity, above it PCM boost may clip peaks.
 * The console's physical system-volume setting remains in effect. */
int vt_audio_volume_percent(void);
void vt_audio_volume_set_percent(int percent);
int vt_audio_volume_level_percent(void);
int vt_audio_volume_muted(void);
int vt_audio_volume_toggle_mute(void);

#endif
