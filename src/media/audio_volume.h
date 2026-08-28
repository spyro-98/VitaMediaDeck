#ifndef VITAMEDIADECK_MEDIA_AUDIO_VOLUME_H
#define VITAMEDIADECK_MEDIA_AUDIO_VOLUME_H

/* Shared perceived volume for every audio backend. Values above 100 are
 * implemented by the decoder path as saturated PCM amplification. */
int vt_audio_volume_percent(void);
void vt_audio_volume_set_percent(int percent);

#endif
