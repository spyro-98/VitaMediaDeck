# VitaMediaDeck project specification

## Product definition

VitaMediaDeck is a native media client for homebrew-enabled PlayStation Vita
systems. It plays user-provided local media and streams user-authorized video
from Jellyfin or authenticated WebDAV, SFTP, and SMB servers. Jellyfin is the
only supported media-server provider; file transports require no companion
service.

## Product principles

1. **The user chooses the source.** VitaMediaDeck browses only local storage and
   explicitly configured servers.
2. **Authentication is explicit.** Remote sources require a username and a
   password. Jellyfin returns a session-only access token, while SFTP
   additionally requires host-key confirmation.
3. **Passwords are ephemeral by default.** Secrets never enter `sources.bin`,
   logs, or media history. An explicit opt-in stores them unencrypted in the
   separately advertised `network/passwords.txt` file.
4. **Protocol and decoder ownership remain separate.** A transport produces
   independent seekable byte cursors; the player owns demux, decode, timing,
   rendering, and teardown.
5. **Hardware acceleration is preferred, not assumed.** The complete public
   H.264 hardware backend is used when compatible, with software H.264 kept as
   a fallback.
6. **No media acquisition or export.** VitaMediaDeck neither downloads, extracts,
   converts, uploads, nor copies remote media.
7. **No private platform APIs.** Production code uses redistributable source
   and public VitaSDK interfaces.

## In scope

- Indexed local video and music library.
- Local rename and delete actions with explicit UI confirmation.
- MP3 metadata/artwork and full-screen music playback.
- Authenticated WebDAV browsing over verified HTTPS.
- Authenticated SFTP browsing with SHA-256 host-key pinning.
- Authenticated SMB2/SMB3 browsing.
- Authenticated Jellyfin video-library browsing, server posters, and seekable
  direct play over verified HTTPS.
- Seekable remote H.264 playback with optional AAC audio.
- Per-video local and authenticated-remote playback resume history.
- Local and remote video-cell previews from artwork, embedded covers, or a
  representative frame fallback.
- Protocol-neutral reusable hardware player module.
- English and Italian application catalogues.
- Opt-in disk diagnostics; in-memory diagnostics remain available while the app
  runs.

## Out of scope

- Public-media discovery or third-party catalogue integration.
- Remote media download, export, extraction, transcoding, or upload.
- Credential synchronization, cloud accounts, or a hosted VitaMediaDeck service.
- DRM, decryption, access-control bypasses, or protected media workflows.
- Anonymous/guest remote sources.
- Jellyfin transcoding, HLS, live TV, music libraries, and server discovery.
- A custom H.264 codec or undocumented firmware decoder interface.

## Supported source contract

The reusable player consumes a `VitaHwStreamFactory`. Every `open` call returns
a new handle containing:

- a blocking `read` operation;
- `SEEK_SET`, `SEEK_CUR`, and `SEEK_END` support;
- exact source size;
- an idempotent close operation;
- independent cursor state.

Multiple handles may exist concurrently for the same file because video, the
selected audio stream, an explicitly requested subtitle stream, and asynchronous
cover extraction demux independently. Selectable-track metadata is copied from
the already-open video demux instead of opening a separate discovery handle. A
backend must return a fresh cursor for every `open` and must not share mutable
offsets between handles.

## Remote protocol requirements

### Jellyfin

- HTTPS only, with public-CA validation or an explicitly confirmed SPKI pin.
- Username/password authentication through the Jellyfin API.
- Access token and user identifier retained only for the running session.
- Video-library browsing and Primary-image cover retrieval.
- Original-file direct play with authenticated byte ranges; no HLS or server
  transcoding in the current provider.

### WebDAV

- HTTPS only.
- CA chain and hostname validation enabled.
- Username/password authentication.
- `PROPFIND` for browsing.
- Actual `206 Partial Content` response to a byte-range probe.

### SFTP

- Password authentication.
- Server SHA-256 fingerprint shown and explicitly confirmed before first use.
- Saved fingerprint must match every later session.
- 64-bit stat, read, and seek support.

### SMB

- SMB2/SMB3 authenticated share.
- No guest fallback.
- Signing/encryption depend on server negotiation and should be required by the
  server on untrusted networks.
- Standard SMB port in the current backend.

## Playback architecture

- FFmpeg MOV/MP4 and Matroska demux on custom AVIO cursors.
- H.264 video with runtime selection across all embedded AAC audio tracks.
- Embedded SubRip, ASS/SSA, WebVTT, and MP4 timed-text discovery, background
  demux, styled rendering, and runtime Off/track selection.
- UTF-8 subtitle layout wraps at codepoint boundaries (including text without
  spaces), limits cues to one through four lines, and never splits a multibyte
  character when truncating with an ellipsis.
- Western and Cyrillic UI/subtitle runs use exact-size Inter Medium or SemiBold;
  Japanese, Chinese, and Korean runs use their native PS Vita system PGFs. The
  fully native system-font option remains available for all runs.
- Subtitle settings persist font, foreground/background colors, size, maximum
  width, minimum/maximum lines, and vertical position with V1/V2 settings-file
  migration into the checksummed V3 record.
- User-selectable decoder policy: Auto (`h264_vita` preferred with FFmpeg
  software fallback), HW H.264 only, or SW FFmpeg only.
- Hardware AAC decode for the reusable video player.
- NV12 CDRAM surfaces presented directly through GXM/vita2d.
- The selected audio track's PTS as master clock.
- Bounded PTS reorder window and late-frame recovery.
- Cooperative cancellation and deterministic thread joins.
- Shared VitaTube player gestures: short Start hands supported local media to
  the mini-player, a 900 ms Start hold toggles OLED ECO mode without stopping
  playback, and Select immediately locks/unlocks player input.
- Local and authenticated-remote video mini-player restoration preserves
  position plus the selected audio and subtitle track. The compact player
  reopens through the same HW/SW decoder stack and publishes its live video
  surface; it does not route video through the narrower legacy audio player.
- A bounded software-only thumbnail worker prefers embedded MJPEG/PNG cover
  streams, rejects almost-black or near-uniform results, otherwise seeks to a
  representative H.264 frame, and caches only checked RGB565 pixels. The
  selected cell preempts obsolete viewport requests. Local sidecar artwork
  remains the first choice, with failed loads held in a short negative cache.
- Local paths and non-secret remote endpoint/path fields produce stable,
  distinct history IDs. Credentials are excluded. A recovered session exposes
  an R1 **Start from beginning** action that clears its saved point.

## Storage

- `local_media.idx`: bounded on-disk local index, maximum 65,536 records.
- `network/sources.bin`: maximum 32 non-secret source definitions.
- `network/passwords.txt`: optional plaintext passwords, disabled by default.
- `playback_history.bin`: local and remote resume positions.
- `settings.bin`: application preferences.
- `session_log.txt`: written only when persistent diagnostics are enabled.

## Release gates

- Release build and VPK archive validation succeed in CI.
- No old discovery/acquisition code or documentation remains in the public
  repository.
- Passwords are never logged and are serialized only after the explicit
  plaintext-storage opt-in.
- HTTPS, SFTP fingerprint mismatch, authentication failure, cancellation, seek,
  EOF, and reconnect paths are tested on physical hardware.
- Repeated local and remote sessions close without leaked workers, sockets,
  decoder surfaces, or audio ports.
