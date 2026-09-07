# IPTV host checks (not on-device playback)

## Integrated checks

On macOS with Clang, pkg-config, FreeType and libpng:

```sh
bash tests/iptv/run-host-tests.sh
```

Runs 3,158 source and 54 session assertions with ASan/UBSan, then again with
ThreadSanitizer. Runs 212 UI/navigation/layout assertions and writes four
960x544 host PNGs (EN/IT page and full drawer) to the reported temporary folder.
These use the production page/components, Inter metrics and a stub system
header/player, not a Vita screenshot. No real network requests occur.

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
