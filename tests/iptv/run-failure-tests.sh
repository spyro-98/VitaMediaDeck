#!/usr/bin/env bash
set -euo pipefail
vmd_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_root"
vmd_ffmpeg="${VMD_IPTV_FFMPEG_ROOT:?Set host FFmpeg 6 source/libraries}"
vmd_fixture="${VMD_IPTV_FAILURE_FIXTURE:?Run make-failure-fixtures.py first}"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-iptv-failure.XXXXXX")"
for vmd_sanitizer in address,undefined thread; do
 clang -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize="$vmd_sanitizer" \
  -Isrc -I"$vmd_ffmpeg" -I"$(xcrun --show-sdk-path)/usr/include/libxml2" \
  tests/iptv/hls_failure_test.c src/network/hls_source.c src/network/hls_manifest.c \
  src/network/iptv_manifest.c src/network/iptv_source.c src/network/iptv_session.c \
  "$vmd_ffmpeg/libavformat/libavformat.a" "$vmd_ffmpeg/libavcodec/libavcodec.a" \
  "$vmd_ffmpeg/libavutil/libavutil.a" -lxml2 -lpthread -lm -o "$vmd_output/failure-test"
 "$vmd_output/failure-test" "$vmd_fixture"
done
printf 'Incompatible-input host artifacts: %s\n' "$vmd_output"
