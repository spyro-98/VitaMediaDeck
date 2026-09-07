# IPTV branch: compatibility contract

Status: first integrated **experimental playback preview** on `feature/iptv`.
The IPTV section opens the HLS source through a forward-only session adapter
and separate opt-in HW/SW live APIs. Native build and host validation pass;
actual Vita broadcast playback, audio output and long-session timing have not
been verified. This is not a completed general-purpose IPTV client.

## Using this preview

1. Press **L1 → IPTV** (or tap the app logo and choose IPTV).
2. Choose **Enter link** / Start, enter an authorized HTTP(S) HLS link, then
   select **Play stream** with the d-pad/X or touch. **Square / Use test stream**
   fills the public Mux HLS test URL; it does not play or fetch until Play.
3. **X / the on-screen pause button** pauses. Resume closes both decoders and
   the old connection, then opens a fresh live point. **R1 → Reconnect live**
   does the same immediately. R1 also retains the screen-fit setting.
4. Tap the video to hide/show the HUD. Existing volume, input lock and eco-mode
   controls remain available. L1 navigation or Circle exits and stops IPTV.

The link stays only in RAM; URL query tokens are hidden on the page and no URL
is logged. No supplied channel lists/accounts are bundled. The Mux sample is a
finite HLS fixture, not proof of a long-lived broadcast. No file is recorded.
Choose HW/SW in Settings before playback; AUTO may fall back during startup.
Mid-stream decoder/track changes, subtitles, seeking, loop and IPTV mini-player
handoff are intentionally unavailable in this first preview.

## Integrated baseline

- VitaMediaDeck master: `22db28b` (includes the large-podcast-feed fix after `911b2cf`).
- vita-hw-decoder master: `2569874`.
- vita-sw-decoder master: `03dabef`.
- vita-https master: `c028d30`.

The integrated preview requires matching local `feature/iptv` decoder commits:
HW `12e3a15617bb596970117936869f10e2e188023e` and
SW `67500ecdfcbf55f6fa1302b05aea4245bc7e5c09`. Their master branches remain
unchanged; build all three matching branch checkouts together.

These are local commits; publication is a separate operation. The podcast Home
failure-navigation fix has host regression coverage. The saved Nova Lectio RSS
feed reproduced the 2 MiB size rejection; the bounded 8 MiB feed reader handles
all 283 episodes in host tests. Console playback still needs a retest.

## Implemented source milestone

- Bounded media/master HLS parsing, final-response-relative HTTP(S) URLs,
  sequence numbers, target duration, end-of-list and discontinuity identifiers.
- Conservative master-variant selection: explicit multiplexed H.264 (Baseline,
  Main or 8-bit High) and AAC-LC codec metadata, resolution and bitrate filters.
  A media playlist has no codec guarantee; packet alignment is validated but
  actual stream codec probing now runs through the opt-in decoder input.
- One producer polls/fetches; independent audio/video cursors consume the same
  cached segment bytes without opening extra media connections. Three consecutive
  transport failures terminate retries, while cancellation is a distinct result.
- Default cache: at most 12 MiB retained compressed payloads, eight segment slots,
  4 MiB maximum incoming segment, plus at most 256 KiB playlist response and
  fixed parser metadata. These budgets exclude TLS and decoded video/audio.
- Readers get distinct WAIT, END, DISCONTINUITY and LAGGED states. Cache eviction
  never silently advances a lagging cursor. A timeline reset requires explicit
  coordinated re-anchoring before decoding resumes. The session now pins the
  first segment until both demuxers have opened, then advances the retention
  floor with the slowest cursor. Reservation before a fetch prevents repeatedly
  downloading a segment while backpressured.
- The production HTTP adapter retains `vita-https` verification/downgrade
  protection and does not log stream URLs or credentials.

The first subset rejects encryption, fMP4 initialization maps, byte-range
segments, low-latency parts, delta playlists, gaps, separate A/V renditions and
oversized playlists/segments. It does not implement adaptive switching, M3U
channel catalogues, EPG, recording or timeshift. HLS VOD fixtures are useful for
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
212 host UI/navigation/layout assertions cover EN/IT text, token redaction,
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
Do not add a complete UI/catalogue before this hardware acceptance gate.
Evaluate FFmpeg's HLS demuxer for broader format support instead of silently
expanding this limited transport into an untested full HLS implementation.

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
