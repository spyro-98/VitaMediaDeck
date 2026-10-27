# Mediaset HLS timestamps and IPTV footer — 27 October 2026

Standard 1.4.1 / APP_VER 01.05, application-side correction. Neither decoder
library, clock policy nor broadcaster authentication is changed.

## Observed cause

The console log reaches the Mediaset HLS track probe successfully: H.264
512×288 and AAC 48 kHz stereo are recognized, then the remux stops with generic
`VT_IPTV_INVALID` after four or five packets, before producing a startup chunk.
The same saved Canale 5 URL reproduces this in the actual host HLS producer:
HTTP responses succeed, and FFmpeg rejects a video packet with PTS 271800 below
DTS 273600. This is not a failed playlist parse or evidence of missing credentials.
The previous UI mapped generic invalid data to a manifest error, misleadingly
blaming the server's playlist. Other channels/URLs can still have separate
transport, geographic, codec or protection constraints.

The observed MPEG-TS video mixes PTS-only and explicit PTS+DTS PES packets,
with B-frame reordering and some DTS later than PTS. This class needs more decode
lead than the previous TS assumption of zero. The correction observes a bounded initial reorder window before any
packet is published, measures the needed lead, and applies one constant offset
to video DTS. Presentation timestamps and audio timestamps remain unchanged.
Already usable TS timestamps retain zero additional lead; low-delay PTS=DTS
fixtures stay unchanged. Existing fMP4 handling remains in place.

The startup queue holds packet references, at most 128 packets / 2 MiB, and is
only used for reordered MPEG-TS. The acceptable correction is bounded by the
stream's reorder/fps metadata and a two-second absolute cap. Unsupported later
clock changes still fail explicitly rather than silently shifting playback.
There is no new worker, per-frame allocation after replay, transcoding, stored
broadcast media, account flow or DRM workaround in the app.

## Error reporting and visible UI

Recognized streams now distinguish unusable timestamps, media-data errors and
remux failures from malformed HLS/DASH playlists. Missing timestamps are no
longer labelled encrypted media. The IPTV catalogue footer displays a status
only when there is an actual message; it does not repeat the visible action bar
and toolbar as a second command legend.

## Verification

- A generated 12-second 512×288/50 fps H.264/AAC fixture uses non-pyramid
  B-frames and PTS-only video PES headers. Compressed media bytes are intact;
  DTS fields are replaced with valid PES-header stuffing. A second fixture
  retains mixed PTS-only/PTS+DTS headers with explicit DTS shifted by one frame.
  Both reproduce negative composition offsets without changing PTS. No broadcaster
  media/keys or private URLs are committed.
- Before the fix, this timestamp class is rejected during remux. Afterward,
  the production session's two readers decode all 600 video and 564 audio
  frames. Initial A/V delta is about 21 ms and final delta about -9 ms.
- The thirteen-fixture HLS suite runs with ASan/UBSan and TSan, including ordinary
  TS, fMP4, negative composition offsets, low-delay TS/fMP4 with PTS=DTS,
  separate renditions, byte ranges, AES-128 and key rotation. Lifecycle and
  real incompatible-input classifications are also checked.
- On the real saved Canale 5 source, the corrected app producer emits six
  startup chunks and the resulting H.264/AAC TS decodes without errors on the
  host. This is a short network/host check, not proof of Vita HW playback or
  of every Mediaset channel. Cine34 HD and Iris HD also emit six chunks and
  decode without host errors (measured decode leads about 40 ms; Canale 5 about
  20 ms). Labels in the source list do not prove the selected resolution.
  The public Italia 1 entry fails DNS (curl 6) before a playlist is received;
  that is a distinct stale/unreachable-host problem, not fixed by remux timing.
- Production IPTV UI dispatch is checked in all eight themes and both
  languages, including absence of the duplicate command legend. ARM syntax,
  final standard Release/LTO linking and release/native audits are recorded
  with the package. No firmware, emulator or ARM JIT is executed.

## Console acceptance

Install the updated standard VPK. Retry the same Mediaset channels, including
Canale 5, in AUTO/HW; check audio sync, continued playback and normal cancellation.
Check the Saved and opened-playlist screens: visible actions should have no
second command legend below. A remaining channel error should have a precise
cause; retain its `session_standard.txt` for comparison. The successful host
probe does not establish resolution/FPS or decoder performance on the Vita.
