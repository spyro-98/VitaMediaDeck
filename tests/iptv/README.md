# IPTV host checks (not on-device playback)

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
nine 12-second fixtures through both production session readers: TS, fMP4,
signed composition offsets, separate video/audio, byte ranges, ranged init maps,
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
