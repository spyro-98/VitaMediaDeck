# IPTV branch: compatibility contract

Status: first source-layer implementation. `iptv_manifest`, `iptv_source` and
`iptv_http` implement the bounded MPEG-TS HLS subset described below. This is not
yet connected to the player or UI; there is no end-user IPTV playback section
in the VPK. No HW/SW decoder code, public layout or function signature changed.

## Integrated baseline

- VitaMediaDeck master: `22db28b` (includes the large-podcast-feed fix after `911b2cf`).
- vita-hw-decoder master: `2569874`.
- vita-sw-decoder master: `03dabef`.
- vita-https master: `c028d30`.

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
  actual stream codec probing belongs to the upcoming decoder adapter.
- One producer polls/fetches; independent audio/video cursors consume the same
  cached segment bytes without opening extra media connections. Three consecutive
  transport failures terminate retries, while cancellation is a distinct result.
- Default cache: 12 MiB retained compressed payloads, eight segment slots,
  4 MiB maximum incoming segment, plus at most 256 KiB playlist response and
  fixed parser metadata. These budgets exclude TLS and decoded video/audio.
- Readers get distinct WAIT, END, DISCONTINUITY and LAGGED states. Cache eviction
  never silently advances a lagging cursor. A timeline reset requires explicit
  coordinated re-anchoring before decoding resumes.
- The production HTTP adapter retains `vita-https` verification/downgrade
  protection and does not log stream URLs or credentials.

The first subset rejects encryption, fMP4 initialization maps, byte-range
segments, low-latency parts, delta playlists, gaps, separate A/V renditions and
oversized playlists/segments. It does not implement adaptive switching, M3U
channel catalogues, EPG, recording or timeshift. HLS VOD fixtures are useful for
transport tests but do not prove long-running live playback.

Validation: 3,158 host assertions under ASan/UBSan and separately ThreadSanitizer,
including concurrent readers, malformed/truncated inputs, eviction, discontinuity,
bounded retry, cancellation and HTTP response limits. A real Mux HLS test fetched
one master, one media playlist and three segments (3,145,052 bytes); both cursors
matched those bytes with five total HTTP requests. Native `-O3` compilation
produces `build/libvitamediadeck_iptv_source.a` without linking it into the current
player. See [reproduction commands](../tests/iptv/README.md).

Next: add a narrowly scoped, opt-in live adapter and verify H.264/AAC demux/clock
behavior on a real Vita. Do not map WAIT to EOF or simulate a seekable full file
just to satisfy the existing interface. Only then expose the IPTV UI/catalogue.
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
