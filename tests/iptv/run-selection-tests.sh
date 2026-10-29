#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_ffmpeg="${VMD_IPTV_FFMPEG_ROOT:?Pinned FFmpeg 6 host source/libraries}"
vmd_fixture="${VMD_IPTV_SELECTION_FIXTURE:?Generated selection fixture directory}"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-iptv-selection.XXXXXX")"
for vmd_sanitizer in address,undefined thread;do
 clang -O1 -g -Wall -Wextra -fsanitize="$vmd_sanitizer" -Isrc -I"$vmd_ffmpeg" \
  -I"$(xcrun --show-sdk-path)/usr/include/libxml2" tests/iptv/selection_demux_test.c \
  src/network/iptv_catalog.c src/network/hls_manifest.c src/network/hls_source.c src/network/iptv_manifest.c \
  src/network/iptv_source.c src/network/iptv_session.c src/network/iptv_options.c src/network/dash_manifest.c \
  "$vmd_ffmpeg/libavformat/libavformat.a" "$vmd_ffmpeg/libavcodec/libavcodec.a" \
  "$vmd_ffmpeg/libavutil/libavutil.a" -lxml2 -lz -lpthread -lm -o "$vmd_output/test"
 "$vmd_output/test" "$vmd_fixture"
done
printf 'IPTV selection host artifacts: %s\n' "$vmd_output"
