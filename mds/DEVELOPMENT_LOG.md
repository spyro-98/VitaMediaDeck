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
  OLED-black planes, amber acquisition geometry, scene identity blocks,
  full-height navigation and player drawers, album-aware signal fields, and a
  Reduce motion-safe ambient layer.
- Restored the VitaTube player contract across music, video, and the compact
  player: short Start minimization, Start-hold OLED ECO mode, and immediate
  Select input lock. Local video resumes at the background position with the
  previous audio/subtitle selections.
- Added a cool machine-telemetry palette and deterministic memory-scan sweep
  while retaining amber for focus and direct action.
- Added progressive cover previews to local and authenticated-remote video
  cells: sidecar artwork remains first, then embedded cover art, then a bounded
  representative-frame fallback with a checked RGB565 disk cache.
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
