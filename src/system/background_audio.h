#ifndef VITAMEDIADECK_SYSTEM_BACKGROUND_AUDIO_H
#define VITAMEDIADECK_SYSTEM_BACKGROUND_AUDIO_H

/* Lease for the system BGM port.
 *
 * sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, ...) selects the audio
 * output type, but on its own it doesn't ask the shell to keep it active
 * when VitaMediaDeck isn't the foreground app. sceAppMgrAcquireBgmPort() is the
 * public VitaSDK contract meant expressly for that case.
 *
 * The lease is deliberately kept separate from the sceAudioOut port: the
 * caller opens/closes the audio port and then releases the lease, on the
 * same thread and in reverse order relative to acquisition. */
typedef struct {
	int acquired;
	int acquire_result;
} VtBackgroundAudioLease;

/* Returns the sceAppMgr result. A failure doesn't prevent the caller from
 * still trying the normal foreground output: it allows a clean degradation
 * when another application already holds the BGM port. */
int vt_background_audio_acquire(VtBackgroundAudioLease *lease);

/* No-op if the acquisition wasn't successful. Returns the sceAppMgr result. */
int vt_background_audio_release(VtBackgroundAudioLease *lease);

#endif /* VITAMEDIADECK_SYSTEM_BACKGROUND_AUDIO_H */
