#ifndef VT_AUDIO_PLAYBACK_H
#define VT_AUDIO_PLAYBACK_H
#include <vita_audio_playback.h>
/* One PCM/output engine shared by both independent video session modules. */
enum { VT_AUDIO_DECODE_AUTO=0, VT_AUDIO_DECODE_HW=1, VT_AUDIO_DECODE_SW=2 };
const VitaAudioPlaybackDriver *vt_audio_playback_driver(void);
const VitaAudioPlaybackDriver *vt_audio_playback_driver_for_mode(int mode);
#endif
