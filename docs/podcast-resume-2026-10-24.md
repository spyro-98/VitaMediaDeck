# Podcast resume reliability — 24 October 2026

App 1.4.1 / APP_VER 01.05 follow-up. Test identity `podresume-1024`, bubble
`VMD podresume-1024`, log `ux0:data/VitaMediaDeck/session_podresume-1024.txt`.
Includes the preceding Jellyfin L1 provider change; decoder packages are unchanged.

## Corrections

The old checkpoint path ran synchronous full-history writes and a volume sync
from a render-loop pump every 15 seconds. An activated job could temporarily
publish zero while buffering or restoring, and a checkpoint could remove its
previous bookmark. Failed saves were acknowledged as if successful; suspend
had no dedicated checkpoint callback. Initial MP3/AvPlayer seek results were
ignored. An audio-output error could let the decoder advance without playback.

A separate low-priority saver now samples the last **decoder-confirmed** podcast
position once a second, independently of fullscreen, mini-player, menus or UI
loading. Provisional timeline positions and startup/errors cannot replace it.
MP3, FLAC and AvPlayer publish confirmed positions; unsuccessful initial seeks
fail instead of silently starting at zero. MP3/AvPlayer audio-output errors
stop advancement and preserve the last confirmed point.

The saver writes a 56-byte versioned record with CRC, stable episode key,
position/duration and base history generation. A synced temporary file is
published with backup/rename recovery. Only that small file is synced; the
periodic path never flushes the whole volume. Writes coalesce when unchanged,
run roughly every 15 seconds while playing, retry failures with a five-second
backoff, and respond to pause and power suspend/resume requests. Callback I/O
runs on the saver thread. Shutdown joins it before teardown.

At startup the latest valid record is merged into the existing history. A newer
normal stop/rewind wins over an older checkpoint; zero-position podcast records
retain a generation so old progress cannot reappear. Failed history updates
roll back in memory, and failed import retains a memory-only recovery point.
The existing database format and podcast keys remain compatible.

Feed durations reach the player and are not overwritten by bitrate estimates.
Downloaded podcasts with app-owned sidecar metadata receive resume handling
outside the standard podcast roots as well. Normal stop retains the full
history update; there is no full-history write on the audio or render loop.
A forced bubble close can lose roughly the latest checkpoint interval. Progress
below ten seconds and completed episodes start from the beginning, as before.

## Evidence and remaining acceptance

The ASan/UBSan and TSan fixtures pass restart, paused/zero/completed progress,
CRC and backup/temp recovery, generation conflicts, failed history import,
partial writes, retry, callback dispatch and asynchronous storage ownership.
A one-hour virtual run produces **240 checkpoints / 13,440 payload bytes**.
Filesystem metadata adds I/O; this is not a Vita latency or battery measurement.
The real host MP3 fixture passes CBR/VBR, local/stream readers, 30-second resume,
paused rewind, failed initial seek and audio-output failure cases. Online-audio,
offline metadata and shared UI/guide regressions are also run. The old online
fixture's shared assertion counter was made atomic to remove a test-only race.

The native Release/LTO VPK and archive audit remain separate from console proof.
The [VitaSDK power callback API](https://docs.vitasdk.org/group__ScePowerUser.html)
and [power event flags](https://docs.vitasdk.org/group__ScePowerKernel.html) define
the callback integration; real delivery/timing still needs device validation.

1. Stream an MP3 episode for at least a minute, note the position, then close
   its bubble without stopping playback. Relaunch and select the same episode;
   verify resume within roughly the last checkpoint interval.
2. Repeat while paused, minimized, in another menu and during a loading screen.
   Test power-button suspend/resume during playback and while paused, then
   close/reopen the app and verify the bookmark. An interrupted transport may
   require selecting the episode again; checkpointing does not reconnect it.
3. Seek backward/forward, pause and stop; restart repeatedly. A previous compact
   record must not undo a newer rewind, completion or reset to the beginning.
4. Interrupt connectivity during initial resume; reconnect and reopen the same
   episode. A failed start must preserve the older point.
5. Repeat with M4A/AAC, FLAC where available, and downloaded podcasts in standard
   and custom folders. Verify readable logs, uninterrupted audio and UI response.

These tests apply to every theme because the playback/history service is shared.
