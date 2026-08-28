# Background audio on PlayStation Vita

## Public API path

VitaSDK exposes two complementary mechanisms:

- `sceAppMgrAcquireBgmPort()` / `sceAppMgrReleaseBgmPort()` negotiate
  background-music ownership with the shell;
- `sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, ...)` opens the PCM output
  port used by the application.

VitaMediaDeck acquires the AppMgr lease before opening the BGM port and releases the
audio port before releasing the lease. If another application owns the resource,
the failure is reported and normal foreground behavior is preserved where
possible.

## Local media ownership

The background service accepts only local files. MP3 is decoded through mpg123;
other supported local A/V files use the public Vita player path. The service
owns pause, volume, seek position, artwork, optional decoded video surfaces, and
mini-player state until the user stops playback.

It never downloads, replaces, renames, or deletes the source file. Stop and
shutdown are cooperative and join the active worker before clearing session
state.

## Video and the mini-player

Retaining an audio port does not preserve an arbitrary video decoder. A player
whose video output is never drained can stop producing audio. The local
background service therefore owns the complete local session and exposes the
latest safe video surface to the mini-player only when the active backend
supports it.

Static artwork remains the fallback for audio-only sources and incompatible
video paths.

## Shell quick controls

The public VitaSDK surface does not provide a supported contract for a normal
VPK to publish title, artwork, playback state, and callbacks into the shell's
music widget. Undocumented shell symbols are outside VitaMediaDeck's production
scope.

## Hardware validation matrix

- Play local MP3, M4A/AAC, WAV, and A/V files for at least 20 minutes.
- Enter LiveArea, return, and repeat foreground/background transitions.
- Turn the display off and on while music continues.
- Pause, seek, and change volume before and after every transition.
- Test while another application owns the BGM port.
- Stop playback and close VitaMediaDeck while decoding is active.
- Verify balanced acquire/open/close/release events in opt-in diagnostics.

Compilation proves symbol availability and resource ordering. Only physical
hardware can validate shell policy on a specific firmware/plugin combination.
