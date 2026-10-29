# IPTV host checks (not on-device playback)

Current 1.4.2 browser checks: `browser_layout_test.c` covers distinct playlist and
channel collections, separate screens, two panes and the actual R1 switches in
all eight themes and IT/EN. `browser_scroll_test.c` verifies independent touch
inertia/analog pixel scrolling, catalogue/selection preservation while switching
and returning to the browser after an invalid playlist. `quality_flow_test.c`
asserts that startup and Automatic never create quality trials: only explicit
R1 calibration does. It also checks cancellation and favourite resolution.
`tests/ui_crossfade/preferences_test.c` covers layout persistence, unrelated bits
and failed-write rollback. Set `VMD_IPTV_UI_CASES` to select individual UI cases.

## IPTV variants, languages and programme data

With the pinned FFmpeg 6 host source/libraries (including the existing custom
HLS I/O patch):

```sh
VMD_IPTV_FFMPEG_ROOT=/path/to/ffmpeg6 bash tests/iptv/run-options-tests.sh
python3 tests/iptv/make-selection-fixtures.py /tmp/vmd-selection-fixtures
VMD_IPTV_FFMPEG_ROOT=/path/to/ffmpeg6 \
VMD_IPTV_SELECTION_FIXTURE=/tmp/vmd-selection-fixtures \
  bash tests/iptv/run-selection-tests.sh
bash tests/iptv/run-ui-tests.sh
```

Options fixtures exercise HLS/DASH high-quality-only masters, explicit video and
audio IDs, group compatibility, rotating signed query strings, missing choices,
XMLTV matching/current-next/timezones/locales, plain/gzip guides, malformed XML,
external-entity rejection and cancellation. ASan/UBSan and TSan are both run.
Real producer/session/FFmpeg tests decode five 6-second selections: Auto SD,
1080p at 9 Mbps with Italian or English external audio, and Italian/English
embedded MPEG-TS tracks. Independent audio rates verify that the requested
track actually changes. Embedded labels survive reconnects without another fetch.

The real IPTV loop also tests failed-quality rollback, while the panel renderer
checks 64-item lists, long labels, active badges and scrollable programme data
in IT/EN across eight themes. Programme data is a synthetic fixture; previews
are host images, never console screenshots. The former direct-1080p negative
fixture now must enter the remux buffer successfully. Other incompatible codec,
AAC layout, HTML, cancellation and timing checks remain separate.

## Guided playlist flow and spatial navigation

```sh
bash tests/navigation/run-host-tests.sh
bash tests/iptv/run-ui-tests.sh
```

The second command runs the production IPTV loop with the real M3U parser,
ASan/UBSan, Inter/IBM Plex Mono metrics and substituted host rendering. It
covers all eight themes in IT/EN: Cross/D-pad-only addition, manual URL or QR, automatic naming, validated
save, channel playback handoff and Back; both presentations; partial last rows;
accessible header options; cancelled/invalid scans; failed storage and retry;
existing saved-name preservation; and open-without-saving. The Add action does
not open a second naming keyboard.
No network request or real playback occurs. Synthetic signed-query URLs must
not appear in displayed labels. It produces PNGs for the empty collection,
URL form, ready form, channels and saved collection. These are host previews,
not console screenshots.

## QR / IPTV errors follow-up

The host suite now passes QR output through the production input handler and
list loader into the real M3U parser. HTTP checks require the full broadcaster
User-Agent header (including 136- and 255-byte cases), canonical URL schemes,
HTTP 429/5xx classification and exact byte ranges. Error mapping distinguishes
manifest/data, video/audio codec, resolution, AAC format and worker setup failures.
Synthetic incompatible-input checks also exercise the real HLS producer with
MPEG-2 video, AC-3 audio, direct 1080p, AAC 5.1 and an HTML response:

```sh
python3 tests/iptv/make-failure-fixtures.py /tmp/vmd-failure-fixtures
VMD_IPTV_FFMPEG_ROOT=/path/to/host-ffmpeg-6 \
VMD_IPTV_FAILURE_FIXTURE=/tmp/vmd-failure-fixtures \
  bash tests/iptv/run-failure-tests.sh
```

The standalone [QR tests](../qr/README.md) compile the actual scanner handoff.
The [crossfade suite](../ui_crossfade/run-host-tests.sh) additionally exercises the
real POSIX loader and verifies that creation failure never runs a task on the UI
thread; it renders wrapped notices in all eight themes and both languages.
These checks do not establish successful camera scans or IPTV playback on Vita.

Part of the 1.4.1 validation set. See [suite index and evidence boundaries](../../docs/VALIDATION.md).

## Expanded HLS transport

The pinned FFmpeg 6 host build must enable `hls,mpegts,mov,aac` demuxers,
`h264,aac` decoders and `mpegts` muxing, with
`tools/ffmpeg-hls-custom-io.patch` applied. Networking remains disabled.

```sh
vmd_hls_fixture="$(mktemp -d /tmp/vmd-hls-fixture.XXXXXX)"
python3 tests/iptv/make-hls-fixtures.py "$vmd_hls_fixture"
VMD_IPTV_FFMPEG_ROOT=/path/to/ffmpeg-n6-host \
VMD_HLS_FIXTURE="$vmd_hls_fixture" bash tests/iptv/run-hls-tests.sh
```

OpenSSL independently encrypts generated media with AES-CBC. The suite decodes
thirteen 12-second fixtures through both production session readers: TS, fMP4,
signed composition offsets, PTS-only and shifted-explicit-DTS TS B-frames, low-delay TS/fMP4 with unchanged PTS=DTS,
separate video/audio, byte ranges, ranged init maps,
explicit/implicit IVs and rotating keys with METHOD=NONE. It also covers malformed
padding/metadata, redirect downgrades, exact ranges, repeated open/close and
cancellation while fetching manifests, keys and segments. Both ASan/UBSan and
ThreadSanitizer runs are required. Existing source, UI, catalogue and DASH tests
remain separate regressions; these tests are not hardware proof.

## Integrated checks

On macOS with Clang, pkg-config, FreeType and libpng:

```sh
bash tests/iptv/run-host-tests.sh
```

Runs source and session assertions with ASan/UBSan, then again with
ThreadSanitizer. Also checks a 300-segment/30-minute HLS VOD and the shared
nonzero timestamp origin used by both decoder implementations. Runs IPTV and
catalogue UI/navigation/layout assertions and writes 960x544 host PNGs (EN/IT
IPTV page, channels, drawer, countries, genres, radio and Podcast Home) to the
reported temporary folder.
These use the production page/components, Inter metrics and a stub system
header/player, not a Vita screenshot. No real network requests occur.
Availability probes additionally cover DNS/403/404 classification, early body
abort, cancellation, HTTPS downgrade rejection and keeping uncertain responses
visible. UI tests cover Show all, offline/cancelled scans, skipped playlist
entries and cached in-player favourite queries with failed-save rollback.
The refinement checks also pack and byte-compare all 251 country flags, test
square/portrait/landscape/alpha artwork, and exercise contextual options,
disabled/touch actions, mutually exclusive panels and the Add stream form.
The options/menu/episode layouts are rendered in both languages as well.
The compact-layout pass also renders the production breadcrumb text, full and
mini-player catalogue viewports, and checks long UTF-8 paths, URL redaction,
leaf priority, header status clearance and relocated tab touch targets.

For real JSON save/remove/rollback checks, also provide a host-built Jansson
prefix (with `include/jansson.h` and `lib/libjansson.a`). An optional local copy
of the reported public M3U is tested without making a network request:

```sh
VMD_JANSSON_HOST_ROOT=/absolute/path/to/host-jansson \
VMD_IPTV_CATALOG_FIXTURE=/absolute/path/to/ita.m3u \
  bash tests/iptv/run-host-tests.sh
```

The September 7 fixture reproduced `result=-1` before the User-Agent capacity
fix and `result=0 entries=322 truncated=0` afterwards. Persistence tests run in
their own temporary directory, never on console storage. They exercise exact
URL removal, repeated removal, independently starred channels, empty storage,
136-character identifiers, failed atomic commit/rollback, backup recovery and
corrupt-file preservation. Parser checks cover BOM/extended headers, whitespace,
512-entry bounds, control injection and unchanged HTTPS downgrade rejection.
The UI tests cover Cancel-first deletion, held opening X, touch confirmation,
write failure, new-link state reset and contextual Save this playlist. They
also render the EN/IT confirmation dialogs.

Search coverage checks filtered index-to-source mapping, playback and starring
the selected result, retained search on return from the player, empty-result
disabled actions, cancellation, case/spacing/accent matching, M3U groups and
channel IDs, metadata JSON round trips, and exclusion of URL query tokens.
The production search toolbar and empty-state layouts are host-rendered too.

HLS coverage allows absent CODECS/RESOLUTION hints and optional subtitle groups,
retains explicit codec/resolution/bitrate rejections, and distinguishes encrypted
media and unsupported A/V rendition groups without retrying them. September 7:
3,266 source assertions per sanitizer, 54 session checks, 3,087 UI checks and
66 search/import/persistence checks passed; the finite FFmpeg 6 fixture passed
4,661 assertions. A real 12 TV Parma HLS check decoded 150 video / 283 audio
frames with 40 ms initial A/V delta. Pluto Horror progressed past its previously
rejected master but stopped at its AES-128 media playlist, still unsupported.
These host checks do not establish on-device playback or long-session sync.

`hls_http_demux_test.c` is an opt-in public HTTP(S) test. Compile the curl adapter
from `http_smoke.c` with `-Dmain=unused_http_smoke_main` into a separate object,
then link it with this test, `iptv_source.c`, `iptv_manifest.c`, `iptv_session.c`
and the same FFmpeg 6 libraries. Pass one authorized HLS URL. It checks two
independent readers and six seconds of decoded A/V without recording or playing
media. The public Mux sample passes 3,772 checks with 23.222 ms initial A/V
offset. AAC packets lacking per-AU PTS use the existing native sample-count
clock model; video timestamp assertions remain strict. This is not Vita TLS,
SceVideodec, speaker-output or long-session proof.

To check demux/decode using the same FFmpeg 6 base as the Vita build, extract
`build/deps/ffmpeg-vita-hw/share/sources/ffmpeg-n6.0-h264-vita-source.tar.gz`
into a temporary directory and build its host libraries:

```sh
./configure --disable-everything --disable-programs --disable-doc \
  --disable-autodetect --disable-x86asm --disable-network --disable-shared \
  --enable-static --enable-avformat --enable-avcodec --enable-avutil \
  --enable-demuxer=mpegts --enable-parser=h264,aac,ac3 --enable-decoder=h264,aac --cc=clang
make -j4 libavformat/libavformat.a libavcodec/libavcodec.a libavutil/libavutil.a
```

Generate a fixture with a host FFmpeg executable that supports libx264 and lavfi.
Replace the example temporary directory with your own existing directory:

```sh
ffmpeg -hide_banner -loglevel error \
  -f lavfi -i testsrc2=size=640x360:rate=30 \
  -f lavfi -i sine=frequency=440:sample_rate=48000 -t 12 \
  -c:v libx264 -preset ultrafast -profile:v baseline -g 60 -keyint_min 60 \
  -sc_threshold 0 -b:v 700k -c:a aac -ac 2 -b:a 96k -output_ts_offset 3600 \
  -f hls -hls_time 2 -hls_list_size 0 \
  -hls_segment_filename /tmp/your-fixture/segment%d.ts /tmp/your-fixture/index.m3u8
# Back in the app repository:
VMD_IPTV_FFMPEG_ROOT=/tmp/your-ffmpeg/ffmpeg VMD_IPTV_FIXTURE=/tmp/your-fixture \
  bash tests/iptv/run-host-tests.sh
```

The fixture check passed 4,661 assertions with FFmpeg 6.0 and 9.0.1: 360 video
frames, 564 audio frames, shared nonzero PTS origin and a 21.333 ms initial A/V
offset. The production session fetches six segments once for both demuxers.
The host demux configuration mirrors the opt-in decoder input; actual Vita
decoder threads, AAC output, display timing and long-running live sessions
still need hardware testing.

## Standalone source checks

On macOS, from the repository root, replace the SDK path if needed:

```sh
vmd_sdk_path="$(xcrun --show-sdk-path)"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined \
  -Isrc -I../vita-https/include -I"$vmd_sdk_path/usr/include/libxml2" \
  tests/iptv/test.c src/network/iptv_manifest.c \
  src/network/iptv_source.c src/network/iptv_http.c \
  -lxml2 -lpthread -o /tmp/vmd-iptv-test
/tmp/vmd-iptv-test
```

Repeat with `-fsanitize=thread` instead of `-fsanitize=address,undefined` to check
the two-reader/one-producer scenario. September 7: both runs passed 3,158
assertions. Native compilation:

```sh
env VITASDK=/Users/eliaspiga/vitasdk cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
env VITASDK=/Users/eliaspiga/vitasdk cmake --build build --target vitamediadeck_iptv_source -j4
```

Optional real-HTTP smoke test, using host curl rather than the Vita TLS runtime:

```sh
vmd_sdk_path="$(xcrun --show-sdk-path)"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined \
  -Isrc -I"$vmd_sdk_path/usr/include/libxml2" \
  tests/iptv/http_smoke.c src/network/iptv_manifest.c src/network/iptv_source.c \
  -lxml2 -lpthread -lcurl -o /tmp/vmd-iptv-http-smoke
/tmp/vmd-iptv-http-smoke https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8
```

This fetches at most three segments, retains bounded compressed data in RAM,
and compares two independent consumer cursors. It writes no media files and
does not decode/play video or sound. The tested Mux fixture returned 3,145,052
matching bytes with five HTTP requests (master, media, three segments).

Limitations: no on-device A/V synchronization proof, no
hardware scheduling test, and no long-lived broadcast acceptance yet. The live
reload/discontinuity/eviction cases use deterministic fixtures; the real network
smoke uses a finite HLS sample. Keep this distinction when reporting results.

## DASH adapter checks

Use the same host FFmpeg 6 source as above, also enabling MOV input, MPEG-TS
output and the H.264 packet-format filter when configuring:

```sh
./configure --disable-everything --disable-programs --disable-doc \
  --disable-autodetect --disable-x86asm --disable-network --disable-shared \
  --enable-static --enable-avformat --enable-avcodec --enable-avutil \
  --enable-demuxer=mov,mpegts --enable-muxer=mpegts \
  --enable-parser=h264,aac,ac3 --enable-decoder=h264,aac \
  --enable-bsf=h264_mp4toannexb --cc=clang
make -j4 libavformat/libavformat.a libavcodec/libavcodec.a libavutil/libavutil.a
```

Generate a new fixture directory with a host FFmpeg executable:

```sh
vmd_dash_fixture=$(mktemp -d /tmp/vmd-dash-fixture.XXXXXX)
ffmpeg -hide_banner -loglevel error \
  -f lavfi -i testsrc2=size=640x360:rate=30 \
  -f lavfi -i sine=frequency=440:sample_rate=48000 -t 12 \
  -c:v libx264 -preset fast -profile:v main -g 60 -keyint_min 60 \
  -sc_threshold 0 -bf 2 -b:v 700k -c:a aac -ac 2 -b:a 96k \
  -f dash -seg_duration 2 -use_timeline 1 -use_template 1 \
  -adaptation_sets 'id=0,streams=v id=1,streams=a' "$vmd_dash_fixture/index.mpd"
VMD_IPTV_FFMPEG_ROOT=/tmp/your-ffmpeg/ffmpeg VMD_DASH_FIXTURE="$vmd_dash_fixture" \
  bash tests/iptv/run-dash-tests.sh
```

The runner checks MPD parsing/security, real two-reader packet remux/decode,
a one-hour timestamp/PTO shift at different audio/video timescales, live MPD
refresh/end, changed periods, truncated fragments, 32-chunk backpressure and
cancel/join under 200 ms. It runs ASan/UBSan and ThreadSanitizer. The fixture
decodes 360 video and 564 audio frames with a 21.334 ms initial A/V difference;
the shifted fixture must produce the same result. Compile-time assertions
check the unchanged 16-byte decoder live-option size/field offsets.

`dash_http_smoke.c` is an opt-in public-network test. Compile `http_smoke.c`
separately with `-Dmain=unused_http_smoke_main`, then link that object with
`dash_http_smoke.c`, `dash_manifest.c`, `dash_source.c`, `iptv_manifest.c`,
`iptv_source.c`, `iptv_session.c`, the host FFmpeg libraries, libxml2, pthread,
math and curl. Pass one authorized DASH URL. It decodes at least six seconds
through both readers, verifies their timeline and the decoded audio shape,
then cancels/joins the producer. No media is saved, rendered or output as sound.
The adapter preserves TLS certificate/hostname and redirect-downgrade checks.
Host curl/FFmpeg results are not Vita networking, SceAudiodec or SceVideodec proof.

### Buffering and saved-playlist regression checks (2026-09-07)

The host suite additionally checks startup targets, protected-cache capacity
fallback, short-VOD EOF and cancellation; HLS/DASH rendition selection rejects
1080p even if its bitrate fits. UI checks cover rename cancellation, whitespace,
Unicode, write failure and retention of favourites, logos and User-Agent, plus
the removal of test-stream actions. HTTP tests distinguish DNS, timeout, TLS,
401/403 and 404 failures without exposing URL tokens.

Both opt-in live decode tools now wait for the app's startup reserve. A current
TV2000 HLS entry from IPTV-Italia decoded 151 video / 283 audio frames after
prefilling three segments (2,705,132 bytes), first A/V delta 37 ms. Canale 5 DASH
decoded 300 video / 142 audio frames after six remux chunks (697,104 bytes),
first A/V delta 20 ms. That feed selected 768x432/50 fps under the existing pixel
budget; it is not a 720p Vita performance test. No media was saved. The console
still needs a long-running 720p test of the new build.

## Cancellation and visible management regression

See [the current implementation and console checks](../../docs/iptv-cancel-management-2026-10-27.md).
`run-host-tests.sh` covers the production form/action-bar dispatch in all eight
themes and both languages, direct QR/save with controller and touch, save-list
and Back preservation, first-channel focus, and cleanup-event preservation.
For the actual player-open worker plus the real source/session producer, run:

```sh
VMD_HOST_FFMPEG=/absolute/path/to/host-ffmpeg \
  bash tests/player_feedback/cancel/run-host-tests.sh
```

This runs ASan/UBSan and TSan, repeating cancellation before prebuffer and during
a decoder stream read. Decoder/transport delivery are synthetic; it requires
the real producer to exit before UI cleanup and checks repeatable stop/join.
Shared loading UI cancellation is exercised by `tests/ui_crossfade/run-host-tests.sh`.

## PTS-only TS / Mediaset regression

See [the timestamp and footer correction](../../docs/iptv-mediaset-timestamps-2026-10-27.md).
Regenerate HLS fixtures with the current generator before running the suite.
The PTS-only case uses synthetic H.264 non-pyramid B-frames and replaces DTS
PES-header fields with legal stuffing, retaining media bytes and PTS. A second
fixture retains mixed PTS-only/PTS+DTS PES and shifts explicit video DTS by one
frame, matching the observed negative-composition class. Both real
session readers must decode 600 video and 564 audio frames with bounded A/V
delta. The ordinary and low-delay fixtures keep their previous expectations.
UI checks reject the duplicate IPTV management legend in all themes/languages
and verify separate timestamp, media-data and remux error mapping.

The photo follow-up also covers explicit saved-link membership without a star
(`saved_membership_test`), 2/3-column navigation and independent pane scrolling.
The catalogue parser accepts a synthetic 512 KiB channel list under its own
8 MiB body cap; playback manifests keep their 256 KiB cap. The reported Peppenamir playlist was reproduced via its QR landing page and
parses to 380 channels after resolving the declared M3U link. Existing
channel-count and line limits remain. `bash tests/iptv/run-catalog-redirect-tests.sh`
checks the shared resolver across arbitrary hosts, unique/ambiguous anchors,
relative URLs, HTML entities, meta refresh, cycles, hop limits and cancellation.
Optional arguments supply an actual HTML response, its effective-URL file,
expected target-URL file and M3U response; these private files are not committed.


Per-channel persistence regression:

```sh
VMD_HOST_JANSSON=/path/to/host-jansson bash tests/iptv/run-playback-restart-tests.sh
```

This links the actual IPTV owner and preference store. Its first child confirms
1080p/Italian and receives SIGKILL while still inside the mock player's live
session. Fresh processes restore the choice with rotated channel/rendition/audio
IDs, preserve it during a temporary missing rendition, and restore it afterwards.
The native-player feedback harness separately checks the real startup callback,
error propagation and trial suppression. Fixtures are local and synthetic;
physical Vita startup/storage behaviour remains a separate acceptance step.


Programme/EPG regression (FFmpeg 6 host source/libraries as above):

```sh
VMD_IPTV_FFMPEG_ROOT=/path/to/ffmpeg bash tests/iptv/run-options-tests.sh
VMD_IPTV_UI_CASES='programme_flow_test player_panel_test' bash tests/iptv/run-ui-tests.sh
```

The options runner tests HLS/DASH discovery plus production XMLTV matching,
streamed gzip expansion above 8 MiB, optional-stop inference, timezone/locales,
ambiguous aliases, country/+1 isolation, cancellation/entities, four-slot raw
cache reuse/failure recovery, public fallback and M3U/provider declarations.
Optionally set `VMD_PUBLIC_EPG_FIXTURE` to a downloaded Open-EPG italy3 gzip, and
`VMD_PUBLIC_EPG_PLAYLIST` to a local M3U fixture, to check current schedules against
real data without making test-network requests. The synthetic core needs neither.
R1 flow tests cover configuration, cancellation/write failure, reset after a
channel URL replaced the input and lazy metadata recovery for recent items.
Panel previews cover all themes and scroll to the actual end of the schedule;
they use host text/control substitutes, not physical firmware pixels.


Channel name ordering:

```sh
VMD_IPTV_UI_CASES='channel_sort_test browser_layout_test browser_scroll_test' bash tests/iptv/run-ui-tests.sh
```

Exercises production sorting/filtering, original-order restoration, selected
identity/artwork-index retention, favourites/search, case/accents/natural numbers,
stable equal names, failed preference writes, every theme and actual R1 D-pad
activation in both presentations. `tests/ui_crossfade/preferences_test.c` covers
persistence across a fresh settings load and preservation of unrelated bits.


Local-file and content import regressions:

```sh
VMD_IPTV_UI_CASES='local_import_flow_test player_saved_panel_test saved_membership_test' bash tests/iptv/run-ui-tests.sh
```

`import_formats_test.c` links `iptv_catalog.c` and `iptv_manifest.c` with libxml2
and ASan/UBSan. It checks mixed/nested M3U/TXT/PLS/XSPF/ASX/URL files, actual HLS
classification, local mount resolution, UTF-16, entities and traversal.
`rotating_session_test.c` additionally links `hls_manifest.c` and takes four
private host fixture paths: two independently fetched Rakuten masters, the
reported .m3u HLS master and a Pluto master with signed paths over 2 KiB.
Fixtures are never shipped because they contain transient session addresses.
The selection/remux/decode runner also exercises local-mount HLS addresses.
The ordinary source suite checks opt-in local IO, ranges, limits and cancellation;
`tests/ui_crossfade/run-motion.sh` covers the production file picker in all themes.


Channel identity and cached logos:

```sh
VMD_IPTV_UI_CASES='identity_flow_test cached_artwork_test' bash tests/iptv/run-ui-tests.sh
```

`identity_test.c` links `iptv_catalog.c` and `iptv_manifest.c` with libxml2 and
ASan/UBSan. It checks explicit HLS/channel session metadata, DASH titles,
M3U/XSPF/local icons, custom names, programme-title exclusion and unsafe XML/
remote-to-local rejection. The real UI flow checks legacy entries, single-file
channel grouping, stored stars/quality, cache-first publication, failed writes
and all eight themes in IT/EN. The options/programme suite checks XMLTV canonical
names and logos; the FFmpeg selection suite checks SDT names from the already
fetched TS prefix. Background programme tests require metadata publication on
the UI thread and exclude cancelled/stale/failed jobs.
