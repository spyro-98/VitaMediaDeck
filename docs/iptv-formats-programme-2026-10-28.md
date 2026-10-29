# IPTV formats, languages and programmes — 28 October 2026

IPTV no longer rejects a stream solely for being 1080p or exceeding 4 Mbps.
The app removes default source caps, fixes zero-as-unlimited checks in both
HLS and DASH remuxers, removes the player post-open 720p rejection and removes
the duplicate live-input 720p guards in both decoder packages. Their shared
frame raster allocation accepts up to 1920×1088 instead of a 720p pixel-area
budget. Codec, AAC channel/rate, encryption, transport, actual allocation and
hardware API errors remain meaningful failures. This is permission to attempt
the format, not a new physical performance claim.

Automatic HLS chooses a lighter compatible video; DASH prefers its existing
lighter pixel-rate policy and falls back to a higher variant if that is all
the source provides. Explicit choices override automatic ranking. Optional
caller-supplied source limits remain available for deterministic fixtures.
Production IPTV does not set those resolution/bitrate limits. Its existing
24 MiB compressed cache remains bounded; per-resource segment allowance is
16 MiB to accommodate larger encoded segments.

## R1 interaction

The IPTV player drawer has Fill screen, Go live/restart, Video quality, Audio
language, Programme and (when automatic decoder mode permits) Decoder. Quality
and language open a scrollable list: arrows choose, Cross commits, Circle
returns. Touch follows the same list/commit path. Current choices are marked.
HLS video choices retain their resolution/bitrate and known unsupported codecs
are labelled. Audio renditions are restricted to the selected video group;
a quality change clears an incompatible language selection. HLS selection IDs
exclude rotating signed queries while keeping resolution/bitrate/group/path.
DASH choices use stable adaptation/representation identities.

A format/language change closes the two readers and their producer using the
existing cancellation/worker cleanup, then reconnects with the selection. A
failed new format restores the previous selection and reconnects once. Explicit
cancellation still exits. The in-app guide documents these controls.

HLS also enumerates AAC tracks inside a multiplexed stream. FFmpeg 6 HLS drops
per-stream MPEG-TS language metadata when it copies child stream parameters.
The app therefore inspects at most 256 KiB of the first already-fetched,
decrypted TS segment with a memory-only MPEG-TS reader. It performs no extra
request, codec decode or nested I/O. Labels are populated before the producer
publishes its first chunk, and remain available after reconnecting. Supported
DASH AAC representations retain their declared language and label.

## Programme information

The M3U parser retains `x-tvg-url` / `url-tvg` and `tvg-id`; saved channels keep
the guide reference and ID. Programme opens the referenced XMLTV guide on a
cancellable loading worker, independently of decoder cancellation. It matches
channel ID and the current time, preferring the UI language. Available title,
subtitle, time range, category, episode, description and next programme appear
in a scrollable panel. Triangle refreshes it. DASH ProgramInformation title
and source are also displayed when declared. Missing or failed guide data is
reported without ending playback. Current live schedules are not assigned to
segmented VOD recordings; only their source-declared information is shown.

Plain XML and gzip are supported with an 8 MiB transfer/expanded guide bound,
a bounded streaming reader and no external entity/DTD loading. Explicit
external XMLTV DOCTYPE declarations are accepted without resolving them;
entity declarations are rejected. Invalid guides are discarded. The source
must provide a guide URL/channel ID for current-programme matching; there is
no guessed programme data or external schedule lookup.

The HLS rendition model follows [RFC 8216](https://www.rfc-editor.org/rfc/rfc8216).
Programme fields and time/channel matching follow the
[XMLTV schema](https://raw.githubusercontent.com/XMLTV/xmltv/master/xmltv.dtd).

## Evidence and delivery

Pinned FFmpeg 6 host runs under ASan/UBSan and TSan verify actual HLS remux and
CPU decoding of 1080p/9-Mbps media, external and embedded Italian/English audio
selection, exact output audio rates and A/V clocks. Options tests cover HLS
and DASH selection and XMLTV/gzip parsing. Existing HLS transport/encryption,
DASH remux/timeline/lifecycle, decoder allocation/pixels (now including full-HD
and padded full-HD), loading cancellation, backend rollback, eight-theme
IPTV UI/playlist/panel and settings/guide regressions pass.

The Release/LTO package is delivered under
`build/releases/1.4.1/iptv-formats-20261028`, keeping the preceding packages.
Validation logs, selected IT host previews, source hashes, package identity
and SHA-256 accompany it. The native app, both decoder libraries and the final
linked PAF imports are checked. Source changes remain uncommitted in all three
repositories. This build has not been installed or tested on physical Vita;
actual hardware playback, native PAF appearance and sustained 1080p performance
remain to be verified there.
