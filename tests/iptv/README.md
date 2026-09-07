# IPTV source checks (not end-to-end playback)

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

Limitations: no player adapter/UI, no actual A/V synchronization proof, no
hardware scheduling test, and no long-lived broadcast acceptance yet. The live
reload/discontinuity/eviction cases use deterministic fixtures; the real network
smoke uses a finite HLS sample. Keep this distinction when reporting results.
