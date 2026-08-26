# VitaWave development status

## Current direction

VitaWave is being developed as a local and authenticated-network media player.
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
