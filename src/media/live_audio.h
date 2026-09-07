#ifndef VMD_LIVE_AUDIO_H
#define VMD_LIVE_AUDIO_H
#include <stdint.h>
typedef struct {
	const char *url;
	volatile int *cancel;
	volatile int *paused;
	void (*update)(void *ctx, unsigned rate, unsigned bitrate, uint64_t position_ms);
	void *ctx;
	void (*buffering)(void *ctx);
	void (*metadata)(void *ctx, const char *station, const char *title);
} VtLiveAudioConfig;
/* Bounded MP3 feed playback. No disk spool and no duration/seek assumptions.
 * Pause closes the transport; resume reconnects to the live broadcast. */
int vt_live_audio_run(const VtLiveAudioConfig *config);
#endif
