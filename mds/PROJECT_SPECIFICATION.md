# VitaMediaDeck project specification

Current application: **1.4.1**, Vita APP_VER **01.05**, TITLE_ID `VMDK00001`.
See [release notes](../docs/releases/1.4.1.md) and [validation](../docs/VALIDATION.md).

## Product and scope

A native media client for homebrew-enabled PlayStation Vita. It browses local
video, audio and image libraries; Jellyfin video; verified HTTPS WebDAV; pinned
SFTP; authenticated SMB2/3; Navidrome music and Immich photos. Radio Browser,
public podcast directories/RSS, HTTP(S)/QR downloads and compatible HLS/DASH
IPTV are integrated. Catalogues do not grant rights to a provider's media.

Seven selectable themes: Legacy (default), Ghost Mesh, Deck 90, Signal,
Aperture, Relay and Mosaic. New themes have native GXM geometry/materials,
independent layouts and shared theme coverage; Legacy retains its own shell.
Home ordering persists per theme. List/page/menu motion uses continuous bounded
transforms rather than replaying each item's entrance. Reduced motion is honored.

## Product principles

1. Sources and service connections are chosen explicitly by the user.
2. Network protocols own byte transport; decoders own demux/decode/timing.
3. Hardware-first Auto selection belongs to the app. HW/SW-only choices remain
   explicit, and cancellation never becomes an excuse to reopen another backend.
4. Credentials are ephemeral by default. The optional plaintext credential file
   is clearly disclosed and removable; Jellyfin access tokens remain memory-only.
5. Downloads are explicit local copies. There is no server-side transcode,
   protected-media acquisition, cloud account or credential synchronization.
6. Source, dependency provenance, license texts and validation limits ship with
   release documentation. Redistributable public VitaSDK interfaces are used.

## Media contract

- H.264 with optional compatible AAC in MOV/MP4 or Matroska. A file extension
  does not add HEVC/AV1/DRM support. MP4 duration falls back to finite A/V stream
  timing when container timing is unavailable; seek requires a seekable source.
- File decoders use independent fresh video/audio cursors, with optional app-owned
  subtitle and thumbnail cursors. The 2.x factory adds cooperative cancellation
  and non-owning abort; it does not permit shared mutable cursor offsets.
- Separate experimental live API: bounded forward-only MPEG-TS, H.264/AAC, two
  identical starting cursors, shared origin. The app remuxes compatible HLS/DASH
  and owns playlist windows, reconnect and transport. Segmented seek is unavailable.
- Hardware decoder samples NV12/P2 direct surfaces; software defaults to copied
  NV12/P2, three workers for seekable media and one for forward-only input.
  Planar P3 remains opt-in; unsupported CPU zero-copy is disabled.
- Owner fences GXM before render completion/restart or surface release. Late
  pictures and compressed reference dependencies are handled separately.
- Embedded text subtitles and provider subtitles are app-owned. Seek preserves
  selection and starts a fresh asynchronous subtitle request at the landed time.
- Eight-grain PCM queue with a dedicated AudioOut consumer; track switches keep
  position, pause and volume, and roll back failures when recovery is possible.
- Local and seekable remote video resume/mini-player preserve position and track
  selection. Live IPTV mini-player handoff remains unsupported.
- Local audio and supported radio/podcasts use fullscreen and persistent mini-player
  controls. Supported formats and image limits are detailed in the main README.

## Browsing, services and trust

| Source | Authentication / transport | Scope |
| --- | --- | --- |
| Jellyfin | username/password; session-only token; HTTPS or explicit LAN HTTP | video libraries, metadata/art, direct Range play |
| WebDAV | verified HTTPS; credentials | PROPFIND, seekable Range reads, download |
| SFTP | password and confirmed SHA-256 host key | browse/read/seek/download |
| SMB2/3 | authenticated share; no guest fallback | browse/read/positioned I/O/download |
| Navidrome | Subsonic-compatible credentials | albums/songs/artwork, music playback |
| Immich | API key | libraries/assets/thumbnails and image viewing |
| Radio / podcast | public directories and explicit user URLs | discovery, favourites, playback and podcast download |
| IPTV / direct links | user-provided HTTP(S) URLs | compatible clear streams, lists and explicit downloads |

TLS verification cannot be disabled. Explicit LAN HTTP is unencrypted. Sensitive
URL paths/tokens must not enter diagnostic logs. User-saved IPTV URLs can contain
credentials and are local plaintext records, not encrypted secret storage.
See [security](../SECURITY.md) and [personal-media setup](../docs/personal-media-2026-10-23.md).

Folder-first and flattened views use cached metadata/thumbnails, bounded
background work, restored folder stacks and shared draw/touch coordinates. Radio
and podcast page changes show cached data before background refresh. Destination
folders are enumerated only on entry/path change/mkdir. No per-frame directory scan.

## Storage, input and presentation

App data lives under `ux0:data/VitaMediaDeck`: local indexes, non-secret source
definitions, optional `network/passwords.txt`, history, settings, favourites,
thumbnail caches and diagnostics. Standard native log: `session_standard.txt`;
explicit test IDs select `session_<id>.txt` and a matching test bubble. Logs
are opt-in on disk and can contain personal paths; redact before sharing.

English/Italian UI; exact-size Inter and Deck 90 mono font, system PGF fallback
for CJK. Subtitle font/color/size/width/rows/position are configurable. The QR
camera crop is shared with the decoder and unobscured by themed decoration.
The app backdrop is themed; native IME keys are controlled by Vita OS.

## Limits and release evidence

No Apple Music/Spotify account playback, YouTube/InnerTube client, protected DRM,
server transcoding, media upload or undocumented firmware decoder interface.
IPTV/provider compatibility is limited by codec, access and transport support.

Host suites, static checks, native compilation and VPK audits are distinct from
physical-console acceptance. Earlier PlayerFix logs and user feedback support
MP4/subtitle seek fixes and HW 720p30 at reported CPU 333 with bus/GPU 222 MHz.
They do not prove all files, 720p60, battery savings or sustained SW 720p30.
The standard app still requests 444/222/222/111 MHz when app clock control is
selected; PSVshell mode remains external. Current 1.4.1 packaging is not a new
physical run or a published GitHub release.
