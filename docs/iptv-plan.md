# IPTV branch: compatibility contract

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

Status: first integrated **experimental playback preview** on `feature/iptv`.
The IPTV section opens the HLS source through a forward-only session adapter
and separate opt-in HW/SW live APIs. Native build and host validation pass;
the user has confirmed DASH playback on Vita. HLS startup/transport fixes and
long-session timing still need a console retest. This is not a completed
general-purpose IPTV client.

## Using this preview

Playback failures show a prominent error notice before returning to the same
channel list, with the reason retained in the footer. In the September 7 La7d
test, the playlist's `/LA7D/CLN/HLS/Live.m3u8` URL returned HTTP 403 on both Vita
and desktop: CloudFront reported a missing `Key-Pair-Id` signature parameter or
cookie. No segment reached either decoder. The broadcaster's current La7 Cinema
page advertises a different signed HLS path and DRM sources. Changing buffers or
decoder settings cannot authorize the unsigned URL in that third-party list;
provider login/signature renewal and DRM integration are not implemented here.

1. Press **L1 → IPTV** (or tap the app logo and choose IPTV).
2. Saved lists/channels are the landing page. **Start / Add stream** opens a
   form with visible Enter/Edit, Open/Play, Save/Rename, Scan QR and Saved buttons.
   Triangle scans QR, Square saves with a name, Select cycles the detected type.
   Inside an opened list, **Save list** is visible and also available with Select;
   the adjacent button stars the entire list. Saved entries expose Rename and
   Remove directly. Up from the first channel enters these actions; Down returns
   to the first channel. Touch uses the same controls. Removal confirms first
   and retains separately saved channels and media. Circle returns a newly
   entered list to its populated form; existing lists return to saved entries.
3. **X / the on-screen pause button** pauses. For live HLS, resume closes both decoders and
   the old connection, then opens a fresh live point. **R1 → Reconnect live**
   does the same immediately. R1 also retains the screen-fit setting.
4. Tap the video to hide/show the HUD. Existing volume, input lock and eco-mode
   controls remain available. L1 navigation or Circle exits and stops IPTV.

Unsaved links stay only in RAM; URL query tokens are hidden on the page and no URL
path or query is logged (failed-request diagnostics include only hosts).
Square / Save link in the form explicitly persists a named list/stream; the
Saved lists/channels page supports starred entries. Square stars a channel,
Select saves/renames an opened list; Star list is visible beside it. R1 switches All / Starred saved entries. Triangle or the
toolbar search button filters saved playlists/channels or the open channel list.
Search matches names, M3U group titles, channel IDs and public URL paths, not
query tokens. It ignores ASCII case, spaces, punctuation and common Latin
accents. Circle navigates back without first consuming Back to clear a search. Search and
selection survive returning from playback; separately saved entries keep their
metadata. Filtering uses a small index map over the catalogue, rebuilt only
when needed, and still requests covers only for visible results. The local saved JSON can
contain access tokens and must not be shared. M3U imports retain up to 512
HTTP(S) entries and supplied `tvg-logo` covers; larger lists report truncation.
The catalogue occupies about 1.7 MiB; artwork remains lazy-loaded for visible
items. Saved references remain bounded at 128. Channel-list imports accept a
UTF-8 BOM, extended EXTM3U headers and surrounding whitespace. Explicit HTTP
entries in an HTTPS-hosted catalogue are separate resources; this does not
relax HTTPS redirects or HLS segment downgrade validation.
No supplied channel lists/accounts/test streams are bundled. No file is recorded.
Choose HW/SW in Settings before playback; AUTO may fall back during startup.
Mid-stream decoder/track changes, subtitles, seeking, loop and IPTV mini-player
handoff are intentionally unavailable for HLS in this preview. Finite HLS VOD
uses FFmpeg's HLS demuxer over bounded HTTP resources (256 KiB source playlists),
starts at the beginning, displays duration, and resumes without reconnecting;
only an explicit Restart returns to the beginning. Direct HTTP Range VOD files
use the existing seekable player and decoder contracts.

### September 7 playlist-management and transport investigation

The reported `https://iptv-org.github.io/iptv/languages/ita.m3u` returned 73,452
bytes / 322 entries. A 136-character User-Agent directive exceeded the old
127-character bound and rejected the entire list, even after the old 128-entry
preview filled up. The shared bounded User-Agent capacity is now 256 bytes in
import, saved JSON, the UI and HTTP transport; header-control injection remains
rejected. The exact downloaded fixture now parses all 322 entries without
truncation. That is import proof, not proof every listed channel is playable.

The mounted console log's HLS failures reproduced with desktop HTTP requests:
`d3749synfikwkv.cloudfront.net` and `live2-mediaset-it.akamaized.net` failed DNS;
`mediapolis.rai.it` returned 403 with the playlist-specified User-Agent; and
MyTivu's Cielo PHP link returned 302 with an empty Location header. The HTTP
client already follows redirects. A final unusable redirect now has a distinct
error and ends startup without repeating the same request three times. No
decoder/buffer adjustment can restore these unavailable or denied sources.
No provider tokens, authorization headers or TLS bypasses were introduced.
Catalogue imports now log transport/parse results, response length, entry count
and truncation without storing URL paths or tokens.

The next console session (21:03) confirmed deletion and the 322-entry import.
It also showed successful HLS master downloads rejected during variant selection,
in addition to obsolete DNS hosts and a 404 for 7 Gold. Selection no longer
requires optional resolution/codec hints or rejects multiplexed A/V simply for
advertising optional sidecar subtitles. Explicit unsupported codecs and the
720p / 4 Mbps limits remain enforced; streams with
missing hints use bounded probing and the existing actual-dimension check.
This follows [RFC 8216 section 4.3.4.2](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.4.2).

The subsequent console log confirmed rendition-group rejection on Mediaset.
The app now configures `hls_source.c`, using the pinned FFmpeg HLS demuxer above
the existing source/session and decoder interfaces. Pluto's AES-128 segments
are decrypted; Inter TV/Mediaset alternate audio is joined with the selected
video. Selection excludes oversized renditions, prefers video over audio-only
variants, and retains one default audio rendition. Optional subtitles are not
downloaded. The old narrow parser remains for legacy zero-config source callers
and their regression fixtures, not the IPTV screen.
A desktop live check of 12 TV Parma decoded 150 H.264 video and 283 AAC frames
through the production HLS source/session with 40 ms initial A/V delta. This
does not prove Vita TLS, HW decode, audio output or long-running playback.

## Integrated baseline

- VitaMediaDeck master: `22db28b` (includes the large-podcast-feed fix after `911b2cf`).
- vita-hw-decoder master: `2569874`.
- vita-sw-decoder master: `03dabef`.
- vita-https master: `c028d30`.

The integrated preview requires matching local `feature/iptv` decoder commits:
HW and SW branch checkouts, including the live-only video timestamp-origin
correction following `12e3a15` / `67500ec`. Their master branches remain
unchanged; build all three matching branch checkouts together.

These are local commits; publication is a separate operation. The podcast Home
failure-navigation fix has host regression coverage. The saved Nova Lectio RSS
feed reproduced the 2 MiB size rejection; the bounded 8 MiB feed reader handles
all 283 episodes in host tests. Console playback still needs a retest.

## Original source milestone (legacy zero-config callers)

- Bounded media/master HLS parsing, final-response-relative HTTP(S) URLs,
  sequence numbers, target duration, end-of-list and discontinuity identifiers.
- Conservative master-variant selection: multiplexed H.264 (Baseline,
  Main or 8-bit High) and AAC-LC codec metadata when supplied, resolution and bitrate filters.
  A media playlist has no codec guarantee; packet alignment is validated but
  actual stream codec probing now runs through the opt-in decoder input.
- One producer polls/fetches; independent audio/video cursors consume the same
  cached segment bytes without opening extra media connections. Three consecutive
  transport failures terminate retries, while cancellation is a distinct result.
- App cache: at most 24 MiB retained compressed payloads, eight segment slots,
  4 MiB maximum incoming segment, plus at most 256 KiB playlist response and
  fixed parser metadata. These budgets exclude TLS and decoded video/audio.
  Startup waits for three HLS segments unless the protected cache fills or a
  shorter finite stream ends. Zero-config transport callers keep 12 MiB and
  one-segment startup. DNS, HTTP 401/403, HTTP 404/410, timeout and TLS failures
  are reported separately; buffering cannot repair unavailable/expired links.
- Readers get distinct WAIT, END, DISCONTINUITY and LAGGED states. Cache eviction
  never silently advances a lagging cursor. A timeline reset requires explicit
  coordinated re-anchoring before decoding resumes. The session now pins the
  first segment until both demuxers have opened, then advances the retention
  floor with the slowest cursor. Reservation before a fetch prevents repeatedly
  downloading a segment while backpressured.
- The production HTTP adapter retains `vita-https` verification/downgrade
  protection and does not log stream URLs or credentials.

The retained legacy subset rejects encryption, fMP4 initialization maps, byte-range
segments, low-latency parts, delta playlists, gaps, separate A/V renditions and
oversized playlists/segments. This HLS parser does not implement DASH:
[the separate DASH adapter](dash-streaming.md) handles supported MPDs above the
same bounded source/session. Adaptive switching, EPG, recording and timeshift
remain unavailable. HLS VOD fixtures are useful for
transport tests but do not prove long-running live playback.

Source validation: 3,158 host assertions under ASan/UBSan and separately ThreadSanitizer,
including concurrent readers, malformed/truncated inputs, eviction, discontinuity,
bounded retry, cancellation and HTTP response limits. A real Mux HLS test fetched
one master, one media playlist and three segments (3,145,052 bytes); both cursors
matched those bytes with five total HTTP requests. Native `-O3` compilation
produces `build/libvitamediadeck_iptv_source.a`, now linked into the app.
Session validation adds 54 assertions under ASan/UBSan and ThreadSanitizer:
late second-reader startup, blocking WAIT (not EOF), prompt abort, fallback
reopen, shared discontinuity and backpressure. A generated 12-second HLS fixture
with a 3,601.378667-second PTS origin passed 4,661 assertions with the pinned
FFmpeg 6.0 base: 360 decoded video frames, 564 audio frames, the expected initial
21.333 ms A/V offset, and six segment fetches shared between two demuxers.
This is host software decode, not SceVideodec/SceAudiodec or speaker/clock proof.
Host UI/navigation/layout assertions cover EN/IT text, token redaction,
error/reconnect routing and all seven section destinations. Host rasterizations
use the real page/components and Inter font but stub the system header/player;
they are not console screenshots. See [reproduction commands](../tests/iptv/README.md).

## Decoder compatibility

Each decoder has a new `vita_*_decoder_live.h` with versioned, size-checked
`LiveOptions` and `open_live()`. Existing public headers, struct layouts,
functions and default seekable-file paths are unchanged. Live inputs require
`size = -1`, NULL seek and a prompt non-owning abort callback. AVIO is explicitly
non-seekable; MPEG-TS duration scans and probe recovery seeks are disabled only
for live inputs. The two demuxers receive one shared PTS origin before decode.
Codec internals, file seek recovery and track-switch logic were not rewritten.
Live seek/track-switch calls reject without disrupting the session.

Next: verify H.264/AAC demux/clock behavior on a real Vita using the preview.
The supplied IPTV-Italia fixture imports 57 entries, including 19 DASH links
now routed to the DASH adapter, and no logo metadata. Import success is not evidence
that those third-party channels are online, unexpired or codec-compatible.
The mounted September 7 log exposed video PTS around 10.033 seconds while
audio had already subtracted the 10.0101-second common origin. The live-only
video conversion now subtracts the same origin before frame reordering and
startup validation; seekable-file conversion remains unchanged. A Vita retest
is required. Host coverage additionally exercises 300 VOD segments (30 minutes)
with one manifest fetch and no 64-segment cutoff.
### Expanded HLS producer

The IPTV screen now uses FFmpeg n6.0 HLS demuxing rather than the legacy parser.
The dependency is rebuilt with HLS/AAC demux support and a small reviewed
custom-IO protocol-recognition patch; FFmpeg networking stays disabled. Every
master, child, key, map and segment request uses the app's verified HTTP(S)
transport. Redirect-relative references are made absolute before parsing.
Master preprocessing chooses one bounded video variant and its default audio;
it never opens all qualities or optional subtitle streams. Fragmented MP4,
packed AAC renditions, MPEG-TS and HTTP byte ranges feed one H.264/AAC TS mux
with a shared timestamp origin. Byte ranges validate status 206, exact offset,
length and Content-Range, including offsets above 4 GiB. Signed MP4 composition
offsets are converted by adding video decode lead without shifting presentation
timestamps or audio; this avoids the TS muxer's PTS-before-DTS rejection.

AES-128 uses libavutil AES-CBC with strict PKCS#7 padding, explicit or
media-sequence IVs, rotating keys and METHOD=NONE transitions. Identity
SAMPLE-AES is delegated to FFmpeg's existing sample-decryption path, but is not
claimed as provider/hardware validated. Non-identity DRM key systems are
rejected; this does not acquire DRM licenses or override access controls.
The supported decoder profile remains H.264 video and mono/stereo AAC up to
48 kHz decoded output; no new HEVC/AV1/AC3 transcoder is implied.

The common compressed cache remains bounded at 24 MiB and startup waits for
six approximately one-second TS chunks. In-flight HLS resources are separately
bounded at 24 MiB / 12 handles (4 MiB per response by default); original
playlists are at most 256 KiB / 8192 nonempty lines, with at most 64 variants
and 64 rendition declarations. Absolute-URI expansion is capped at 2 MiB.
These limits exclude FFmpeg probe/parser allocations, TLS and decoded frames.
Buffers are shrunk to actual size before retention. Segment/key failures stop
both readers after bounded transport retries instead of silently skipping
corrupt media. Nonmonotonic/large timestamp resets still fence the shared
session rather than giving audio and video independent clock epochs.
Part-only low-latency/delta-only playlists, arbitrary codec transcoding,
adaptive quality switching and licensed DRM are not implemented.

`tests/iptv/run-hls-tests.sh` exercises independent OpenSSL AES fixtures, key
rotation, TS/fMP4, alternate audio, byte ranges and initialization ranges under
ASan/UBSan and ThreadSanitizer. Nine 12-second two-reader decode fixtures pass,
plus range, padding, redirect, repeated teardown and cancellation checks at
manifest/key/segment fetches. Public host decoding passed for Inter TV (about
9 ms initial A/V delta) and Pluto (60 seconds, 1801 video / 2813 audio frames,
about 28 ms initial delta, manifest refreshes and key rotation). Canale 5 HLS
also passed 15 seconds (751 video / 353 audio frames, zero initial A/V delta)
after correcting signed composition offsets and small repeated video DTS.
Vita TLS,
hardware decode, audio output and long-session pacing still require retesting.

References: [HLS RFC 8216](https://datatracker.ietf.org/doc/html/rfc8216),
[FFmpeg n6.0 HLS demuxer](https://github.com/FFmpeg/FFmpeg/blob/n6.0/libavformat/hls.c),
[Mux public test streams](https://test-streams.mux.dev/).

## Required boundaries

1. Keep playlist import, authentication, HTTP/HLS connections, retries and
   bounded buffering in a shared app-side source/session layer. Do not duplicate
   connection management inside HW and SW decoding code.
2. Preserve current file/Jellyfin/WebDAV/SFTP/SMB paths and their behavior by
   default. Live playback is explicitly opt-in, not a reinterpretation of all
   existing stream factories.
3. Do not change existing public function signatures or public struct layouts
   for IPTV. Appending fields alone is not binary compatibility. If new live
   capabilities are unavoidable, prefer separate, versioned opt-in APIs/options
   with explicit size/version and retain existing entry points and defaults.
   Check both source and binary interface implications before implementation.
4. Keep codec internals unchanged unless a reproducible live-playback requirement
   demonstrates otherwise. Make corresponding input-contract changes consistently
   in both backends, without rewriting their stable file-seek/audio-track logic.
5. Use one logical live source/timeline for audio and video. Independent cursors
   may share bounded cached data; they must not start separate, unrelated live
   broadcasts. Define buffer eviction, lagging-reader and discontinuity behavior
   before accepting a cached-cursor implementation. A single-demux packet path
   is an alternative to evaluate, not an automatic whole-player refactor.
6. Empty live buffers are not EOF. Reads must remain cancellable; cancellation,
   transport failure and natural completion must remain distinct. Do not fake
   full seekability or a known duration for an endless stream.
7. Preserve the source session across backend changes where feasible, land at a
   valid keyframe and synchronize audio/video/subtitle resets. Do not promise
   seamless switching without hardware evidence.
8. Retain TLS verification, safe redirect handling and token-redacted diagnostics.
   Bound playlist, EPG, artwork, segment and decoded-frame memory independently.

## First implementation gate

Prove a narrowly defined H.264/AAC live source on hardware before building a
complete catalogue. Then add M3U import, channel groups/logos/favorites and XMLTV
guide data. Explicitly report unsupported stream formats. Keep DRM, recording,
timeshift and broad provider-specific account integration outside this first gate.

Regression acceptance includes existing finite-file playback; distant forward
and backward seeks; audio/subtitle track changes; repeated HW/SW changes;
pause/fullscreen/miniplayer transitions; cancellation and reconnect; and long
sessions alongside a background download. Host builds/tests and archive checks
are not substitutes for this Vita acceptance matrix.
