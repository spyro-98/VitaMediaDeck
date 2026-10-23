# DASH streaming preview

Available on `feature/iptv`, from **L1 → IPTV**. `.mpd` links automatically
select DASH, including links imported from M3U lists, scanned by QR or saved
as favourite channels. For extensionless URLs, select DASH using Triangle.
Press Play to connect. Built-in test-stream actions have been removed; developer
fixtures remain outside the app. Pause/resume reconnects a live channel; finite VOD resumes in place.
Segmented VOD is forward-only: arbitrary seeking and mini-player handoff are
not implemented.

## Architecture and compatibility

`dash_manifest.c` reads a bounded MPD and selects one video/audio pair.
`dash_source.c` fetches initialization/media fragments through the existing
verified `vita-https` adapter. Private, memory-only FFmpeg MOV readers extract
compressed packets; an MPEG-TS muxer copies those packets into the existing
IPTV cache/session. No video/audio is re-encoded, no media is written to disk,
and FFmpeg cannot open network URLs or external MP4 data references itself.

Presentation time is computed from each track's timescale and
`presentationTimeOffset`, then normalized against one common A/V origin.
Separate track segment durations need not match. Live audio preroll is trimmed
at a packet boundary to the selected video start. MPD refreshes retain track
identity/timing; an incompatible epoch change stops both readers for an explicit
reconnect instead of silently jumping. Live-to-static MPD transitions drain to EOF.

The decoder packages still use their existing MPEG-TS/H.264/AAC pipelines.
The main file API and structures have not changed. Version 1 of `open_live()`
remains valid. Version 2 uses the same 16-byte options layout to preserve the
decoded AAC sample rate/channel count from MP4, including HE-AAC/SBR, and limits
optional probing for this single-program input. Neither ordinary file seeking
nor file audio-track switching is altered.

## Supported subset and bounds

- Single-period, unencrypted H.264 + AAC-LC/HE-AAC/HE-AACv2 fMP4; mono/stereo,
  output sample rate 8–48 kHz. Both video and audio are required.
- Static and dynamic MPDs; inherited BaseURL and SegmentTemplate attributes;
  `$RepresentationID$`, `$Bandwidth$`, `$Number$`, `$Time$` and bounded `%0Nd`
  formatting; SegmentTimeline repeats, including bounded `r=-1`.
- SegmentList with distinct initialization/media URLs, no byte ranges.
- Direct and HTTP ISO/xsdate UTCTiming where supplied. Duration-addressed live
  streams otherwise rely on a correctly set Vita clock. The client fetches
  complete segments, not low-latency partial chunks.
- Initial selection is capped at 4 Mbps, 1280x720 and a 720p30 pixel-rate budget.
  A 50 fps broadcast can therefore select a lower resolution. No automatic
  mid-playback quality switching is implemented.
- MPD: 256 KiB, 16,000 XML nodes, depth 32, no DTD/entity expansion. Timeline
  repeats are evaluated arithmetically instead of allocating one object per segment.
- App compressed shared cache: 24 MiB, up to 32 remux chunks (HLS retains eight
  segment slots). One incoming fragment per track is capped at 4 MiB; each
  retained initialization is capped at 512 KiB. Output chunks are capped at
  4 MiB. TLS, FFmpeg packet/probe memory and decoded textures/audio are additional.
- App startup preloads six roughly one-second remux chunks; protected-cache
  backpressure and short-VOD EOF can release startup sooner. Live selection moves
  farther behind the edge to leave this reserve plus three fragment durations
  when the server window permits. A/V still use one shared landing point.
  Zero-initialized transport callers retain the original 12 MiB/one-chunk policy.
  Whole-fragment requests remain bounded: a larger socket/request buffer cannot
  ask a CDN for future fragments that have not been published. This policy adds
  latency and cannot overcome sustained download speed below the media bitrate.
- Slow fetches (at least 500 ms) log byte count, duration and result, not signed
  URLs. The player logs the actual startup cache size for Vita retests.
- Cancellation reaches active HTTP and memory readers; only drained finite VOD
  returns EOF. Backpressure retains the common starting chunk until both
  decoders have opened, then follows the slower reader.

DRM/encrypted tracks, unsupported codecs, multi-period manifests, SegmentBase,
byte-range addressing and external renditions/subtitles outside the selected
A/V pair are not supported. DASH support does not make expired, geoblocked or
unavailable provider links playable. Saved URLs may contain private access
tokens; don't share the saved-list JSON or private manifests.

## Validation and reproduction

See [host test commands](../tests/iptv/README.md). Generated fixtures cover
separate segments with unequal durations, B frames, a one-hour sample-clock
offset with different track timescales, live refresh/end transitions,
discontinuity, malformed XML/media, bounded backpressure and cancellation.
The tests decode through two independent MPEG-TS readers under ASan/UBSan
and ThreadSanitizer. They do not execute SceVideodec/SceAudiodec, output sound,
or establish long-running stability on a Vita.

September 7 host network checks decoded at least six seconds from both the
DASH-IF reference and the Canale 5 link supplied by IPTV-Italia. The latter
selected 768x432 at 50 fps with 48 kHz stereo HE-AAC and a 20 ms initial A/V
difference. The reference's revised first demux probe took about 0.5 seconds
on the host; this excludes connection/initial-fragment loading and is not a
Vita startup-time measurement. The user has separately confirmed initial DASH
playback on Vita; this does not validate every provider or the later buffering,
error-handling and long-session changes, which still need a console retest.

Public reference sources: [DASH-IF timing model](https://dashif.org/Guidelines-TimingModel/),
[DASH-IF live simulator](https://livesim.dashif.org/),
[FFmpeg formats](https://ffmpeg.org/ffmpeg-formats.html).
