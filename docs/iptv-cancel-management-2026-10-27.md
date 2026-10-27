# IPTV cancellation and visible management — 27 October 2026

Standard 1.4.1 / APP_VER 01.05; no additional test package, decoder-library API
change or new theme. The console's previous installed executable matches the
preceding standard package. Its session shows both successfully imported lists
and transport/manifest failures; it does not contain a frozen-thread stack.

## Cancellation ownership

During prebuffering the IPTV producer already owns network requests while the
decoder may not yet own a transport. The previous loading callback interrupted
only the decoder. Returning from the open worker then left the UI thread to
cancel and join the producer, which could wait for outstanding I/O without
redrawing. The corrected callback cancels both producer and decoder before
publishing the operation flag. Failed/cancelled initial opens join the producer
on their loading worker. Holding Circle, a fresh Circle press and the visible
touch Cancel button each request cancellation once; the screen continues to
show Cancelling while cleanup runs. Playlist loading uses the same feedback.

A nonblocking producer-exit acknowledgement also covers ordinary player exits
and loading-worker allocation failures: IPTV redraws Closing connection until
the producer exits, then joins and frees it. Callback contexts and readers are
kept alive; threads are not forcibly terminated or detached. Session cleanup
preserves the original failure event. A decoder change uses the reusable open
path, retaining the producer for rollback to the previous backend.

A server/DNS operation may still take time to acknowledge cancellation within
the existing transport limits. This patch keeps the UI rendering during that
wait; it does not claim that every network operation can stop instantly.

## Visible management

- The link form exposes Enter/Edit, Open/Play, Save/Rename, Scan QR, Saved and
  content type. Start edits, Triangle scans, Square saves, Select changes type.
- Saved entries expose Open/Play, Favourite, Rename and Remove; opened playlists
  expose Play, Favourite, Save/Rename list and Star list. R1 keeps extra options.
- Controller focus and touch share geometry and dispatch. Up from the first
  channel enters actions; Down returns to the first channel once.
- Back from a newly entered list restores its populated form. Existing saved
  lists return to the saved collection. Rename/save updates its visible state.
- Themes use their existing shared components, typography, focus animation and
  colour tokens. Filtering remains index-based; no per-frame persistence I/O
  or additional network requests are introduced by the action bar.

## Verification and remaining console checks

The host suites use production UI dispatch, the actual player open worker,
source/session and real POSIX threads. Decoder and system graphics are
substituted; PNGs are layout evidence, not Vita/PAF screenshots.

```sh
VMD_HOST_FFMPEG=/absolute/path/to/host-ffmpeg \
  bash tests/player_feedback/cancel/run-host-tests.sh
VMD_HOST_FFMPEG=/absolute/path/to/host-ffmpeg \
  bash tests/player_feedback/run-host-tests.sh
VMD_JANSSON_HOST_ROOT=/absolute/path/to/host-jansson \
  bash tests/iptv/run-host-tests.sh
bash tests/ui_crossfade/run-host-tests.sh
```

The cancellation regression repeats prebuffer and decoder-read cancellation
20 times each under ASan/UBSan and TSan (400 assertions per run). It requires
producer exit before UI cleanup and checks idempotent stop/join. Its controlled
negative run uses the previous 6af4eb5 player and exits normally with status 1,
identifying the still-blocked producer. Synthetic I/O timing is not a physical
network latency measurement. Session tests also preserve a discontinuity event
after cleanup. Backend-switch regressions retain the producer for rollback.
UI tests cover direct QR/save by controller and touch, saving an opened list,
Back preserving its URL, action-bar focus returning to the first channel, and
all eight themes in English/Italian. Real shared loaders test held Circle,
Circle edges, touch cancellation and restart, including callback-before-flag
ordering and visible cancellation text.

HLS/DASH demux/remux/lifecycle regressions, ARM Release/LTO syntax with warnings
as errors, native import/linking and final release/source audits are recorded
with the standard package. No emulator, Vita firmware or ARM JIT executes.

Physical acceptance of this new build remains necessary:

1. Open a slow IPTV source and hold Circle before buffering, then retry a working
   source. Repeat after buffering starts and with the touch Cancel button.
2. Cancel a slow playlist import; verify animation continues and the form returns.
3. Start → Scan QR → Open list → Save list. Return with Circle and verify the
   URL/name remain, then reopen Saved and check the entry.
4. Rename/remove a saved entry from the visible bar (also touch); cancel removal
   once, then confirm it. Separately saved channels must remain.
5. Switch HW/SW on a supported stream; check rollback if one backend fails.
6. Check native labels, focus animation and the mini-player viewport on the Vita.

The usual `session_standard.txt` identifies the build and records source errors
without logging URL paths or query tokens. Private logs are not release assets.
