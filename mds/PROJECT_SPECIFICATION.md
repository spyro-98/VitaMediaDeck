# VitaWave project specification

## Product definition

VitaWave is a native media client for homebrew-enabled PlayStation Vita
systems. It plays user-provided local media and streams user-authorized video
from authenticated WebDAV, SFTP, and SMB servers. It is self-contained and does
not depend on a companion transcoding or discovery service.

## Product principles

1. **The user chooses the source.** VitaWave browses only local storage and
   explicitly configured servers.
2. **Authentication is explicit.** Remote sources require a username and a
   session password. SFTP additionally requires host-key confirmation.
3. **Passwords are ephemeral.** Secrets never enter `sources.bin`, logs, or
   media history.
4. **Protocol and decoder ownership remain separate.** A transport produces
   independent seekable byte cursors; the player owns demux, decode, timing,
   rendering, and teardown.
5. **Hardware acceleration is preferred, not assumed.** The complete public
   H.264 hardware backend is used when compatible, with software H.264 kept as
   a fallback.
6. **No media acquisition or export.** VitaWave neither downloads, extracts,
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
- Seekable remote H.264/AAC playback.
- Local playback resume history.
- Protocol-neutral reusable hardware player module.
- English and Italian application catalogues.
- Opt-in disk diagnostics; in-memory diagnostics remain available while the app
  runs.

## Out of scope

- Public-media discovery or third-party catalogue integration.
- Remote media download, export, extraction, transcoding, or upload.
- Credential synchronization, cloud accounts, or a hosted VitaWave service.
- DRM, decryption, access-control bypasses, or protected media workflows.
- Anonymous/guest remote sources.
- A custom H.264 codec or undocumented firmware decoder interface.

## Supported source contract

The reusable player consumes a `VitaHwStreamFactory`. Every `open` call returns
a new handle containing:

- a blocking `read` operation;
- `SEEK_SET`, `SEEK_CUR`, and `SEEK_END` support;
- exact source size;
- an idempotent close operation;
- independent cursor state.

Two handles may exist concurrently for the same file because audio and video
demux independently. A backend must not share mutable offsets between them.

## Remote protocol requirements

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

- FFmpeg MOV/MP4 demux on custom AVIO cursors.
- H.264 video and AAC audio track selection.
- `h264_vita` preferred; FFmpeg software H.264 fallback.
- Hardware AAC decode for the reusable video player.
- NV12 CDRAM surfaces presented directly through GXM/vita2d.
- Audio PTS as master clock.
- Bounded PTS reorder window and late-frame recovery.
- Cooperative cancellation and deterministic thread joins.

## Storage

- `local_media.idx`: bounded on-disk local index, maximum 65,536 records.
- `network/sources.bin`: maximum 32 non-secret source definitions.
- `playback_history.bin`: local resume positions.
- `settings.bin`: application preferences.
- `session_log.txt`: written only when persistent diagnostics are enabled.

## Release gates

- Release build and VPK archive validation succeed in CI.
- No old discovery/acquisition code or documentation remains in the public
  repository.
- No password or session secret is serialized or logged.
- HTTPS, SFTP fingerprint mismatch, authentication failure, cancellation, seek,
  EOF, and reconnect paths are tested on physical hardware.
- Repeated local and remote sessions close without leaked workers, sockets,
  decoder surfaces, or audio ports.
