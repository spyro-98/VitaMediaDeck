# VitaMediaDeck development status

## Current direction

VitaMediaDeck is being developed as a local and authenticated-network media player.
The application starts in Local Media and exposes only Local Media, Network
Sources, Settings, and About from the shared sidebar.

## Completed in the current development branch

- Removed online catalogue, search, channel, subscription, favorite, download,
  extraction, and conversion code from the application target.
- Replaced the main application dispatcher with local and remote media flows.
- Preserved the paged local library, local audio player, artwork, metadata,
  resume history, and file actions.
- Added a binary source database that never stores passwords.
- Added WebDAV browsing with HTTPS certificate validation, XML `PROPFIND`
  parsing, and verified byte-range reads.
- Added SFTP browsing/read/seek through libssh2 with explicit SHA-256 host-key
  confirmation.
- Added authenticated SMB2/SMB3 browsing/read/positioned-I/O through libsmb2.
- Added native Jellyfin sign-in, video-library navigation, Primary-image covers,
  and authenticated seekable direct play over verified HTTPS. Provider tokens
  remain memory-only; server transcoding and remote audio are intentionally not
  part of this first provider integration.
- Added one protocol-neutral stream factory shared by local files and all
  remote transports.
- Added metadata discovery and in-player switching for multiple AAC audio and
  embedded text-subtitle tracks over that same local/remote factory contract.
- Added a bounded subtitle demux worker and Unicode-aware renderer that honors
  subtitle font, foreground/background colors, size, outline, width,
  minimum/maximum line count, and position preferences.
- Added exact-size Inter Medium/SemiBold subtitle faces, native Vita
  Japanese/Chinese/Korean PGF fallback, and a per-script Settings preview.
- Rebuilt every shared UI surface around the original Signal / Shell design:
  OLED-black planes, spectral acquisition geometry, scene identity blocks,
  full-height navigation and player drawers, album-aware signal fields, and a
  Reduce motion-safe ambient layer.
- Restored the VitaTube player contract across music, video, and the compact
  player: short Start minimization, Start-hold OLED ECO mode, and immediate
  Select input lock. Local video resumes at the background position with the
  previous audio/subtitle selections.
- Rebalanced the palette around spectral cyan focus, teal machine telemetry,
  silver-white particles, and deterministic memory-scan sweeps. Oxidized amber
  remains only in warning semantics and sparse reflected telemetry.
- Replaced the cartoon-like particle-play icon with a fractured obsidian-glass
  memory shell whose central playback aperture stays legible in the indexed
  128x128 LiveArea export.
- Replaced that material-heavy shell with a sharper memory aperture matching
  the final UI: three optical signal membranes, fine cyan particle reassembly,
  sparse HUD arcs, pitch-black OLED negative space, and one restrained amber
  telemetry trace. The literal Play button is no longer the dominant symbol.
- Added progressive cover previews to local and authenticated-remote video
  cells: sidecar artwork remains first, then embedded cover art, then a bounded
  representative-frame fallback with a checked RGB565 disk cache.
- Moved embedded-cover decoding ahead of stream analysis so indexed Matroska
  and MP4 artwork cannot consume the thumbnail deadline while probing the main
  movie. H.264 frame fallback now probes only when codec parameters are missing.
- Verified a real converted movie whose syntactically valid embedded JPEG was
  entirely black. Thumbnail cache v3 now rejects almost-black and near-uniform
  embedded/cached pictures, seeks farther into long videos, uses fast bounded
  H.264 frame decoding, and records origin plus elapsed time. Selected-cell
  requests preempt stale viewport work; failed sidecars are not decoded every
  render frame.
- Reworked subtitle switching around one persistent demux worker: the initial
  factory open/probe/seek, later track changes, Off, and video seeks are all
  serial requests. They clear stale cues immediately, expose pending/failure in
  the R panel, and never enter the full-screen loading runner. Read-ahead now
  uses a sorted 24-cue active window and a five-second future horizon instead of
  scanning far into sparse SubRip streams. A stable video/audio clock stream
  advances that horizon even when SubRip packets are sparse, and overlapping
  cues are combined instead of blocking later dialogue. Indexed subtitle opens
  trust the selected stream identity already supplied by the playback demux
  rather than probing unrelated audio/video tracks. Superseding requests cancel
  app/network cursor work through a subtitle-private flag, never the live
  audio/video decoder flag. A five-second watchdog changes a stalled request to a
  visible failure, and applying that same track again retries it.
- Added a short cooperative-cancel grace for subtitle switches. A responsive
  local/remote cursor is flushed, repositioned, and reused; only a read that
  fails to retire is destructively aborted and reopened. Explicit player close
  still wakes transport I/O immediately.
- Reduced runtime seek preroll to the first decoded frame with a 600 ms cap in
  both reusable decoders.
- Promoted both decoder AudioOut workers above video scheduling so local AAC
  refill cannot be starved during H.264 or UI bursts. Their teardown summaries
  now record delayed 1024-frame grains and the maximum refill gap without
  logging from the live output loop.
- Removed the app's synchronous third track-discovery cursor. Both decoder
  packages now snapshot AAC and supported text-subtitle metadata from the
  already-open video demux, so startup needs only the video and selected-audio
  cursors while preserving multilingual labels and stable stream indices.
- Fixed the thumbnail resume state that could pass a permanently set cancel
  flag to every later cover request after a scene transition. Results now reach
  the UI before disposable cache persistence, GPU LRU eviction happens before
  replacement allocation, and playback hand-off releases cached cover textures
  before decoder CDRAM startup.
- Replaced synchronous SFTP/SMB hostname resolution with bounded native Vita
  resolver jobs. SSH and SMB connect/auth/stat/open advance in short cancelable
  service slices, and an aborted socket skips protocol-level close waits.
- Restored complete-but-bounded stream discovery for thumbnail and playback
  inputs, preventing indexed long videos and embedded covers from being opened
  with incomplete codec parameters. Failed saved-position seeks now retry from
  the beginning instead of linearly decoding across the file.
- Added a conditional **Active player** destination above Home, fullscreen
  restoration from the mini-player title and the selected active grid cell,
  audio-only expansion guards, and an opening-screen **Play from beginning**
  action. ECO mode now separates its status text from the input-lock badge.
- Extended playback history to stable per-video local and remote IDs, including
  duration-aware completion cleanup and a conditional R1 **Start from
  beginning** action for recovered sessions.
- Extracted the decoder/presenter boundary into the `vita_hw_player` static
  module with public headers and a local-file example.
- Kept the complete public H.264 hardware decoder, AAC path, direct NV12
  presentation, PTS-aware scheduling, and software H.264 fallback.
- Reduced normal settings and application navigation to the new product scope.
- Added reproducible libssh2 and FFmpeg Vita dependency builders.

## Validation completed locally

- Release compilation reaches the final Vita executable.
- The reusable static player target builds with Cortex-A9/NEON optimizations.
- Linkage succeeds against pinned curl/Mbed TLS, libssh2/Mbed TLS, libsmb2,
  libxml2, zstd, and
  the media/UI dependencies.
- Source-level scans can verify that removed product-specific names and paths
  no longer occur in active public files.

Local compilation does not prove network compatibility or decoder lifecycle on
the physical console. Those tests remain mandatory.

## Hardware validation still required

1. WebDAV Basic/Digest authentication against at least two HTTPS servers.
2. Failure on an invalid CA, hostname mismatch, plain HTTP endpoint, missing
   Range support, and expired credentials.
3. SFTP first-use fingerprint flow, correct reconnect, and mismatch rejection.
4. SMB authentication, large positioned reads, EOF, and reconnect behavior.
5. Seek repeatedly in short and long remote H.264/AAC files.
6. Exit while each transport is blocked in I/O.
7. Play at least twenty local/remote sessions to detect leaked threads, sockets,
   audio ports, and CDRAM surfaces.
8. Exercise 480p, 720p30, 720p50, and 720p60 sources with diagnostics off and
   on.
9. Verify local MP3/M4A/AAC/WAV behavior, artwork, mini-player, shuffle, repeat,
   and display-awake preferences.
10. Switch repeatedly between at least two AAC tracks and two text-subtitle
    tracks in local, WebDAV, SFTP, and SMB videos, including seek, pause, loop,
    Off, and decoder-fallback transitions.
11. Verify short Start versus held Start timing, ECO exit, immediate Select
    lock/unlock, local-video mini-player hand-off, and position/track restoration.
12. Verify sidecar, embedded-cover, and frame-fallback previews in local storage,
    WebDAV, SFTP, and SMB grid/list views, including slow and failed connections.
13. Stop and reopen multiple local and remote videos, verify distinct resume
    points, completion cleanup, and the conditional R1 restart action.

## Near-term work

- Complete on-device server matrix testing and improve protocol error details.
- Add touch/held-repeat parity to the Network Sources editor/browser.
- Add a confirmation step before deleting a saved network definition.
- Decide whether remote audio belongs in the same browser after video stability
  is proven.
- Move the remaining implementation support files physically under
  `modules/vita_hw_player` once the public API settles.
- Publish the module as its own repository/release with a standalone CMake
  package and an independently buildable example.
