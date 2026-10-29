#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_ffmpeg="${VMD_IPTV_FFMPEG_ROOT:?Set the pinned FFmpeg 6 host source/libraries}"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-iptv-options.XXXXXX")"
for vmd_sanitizer in address,undefined thread; do
 for vmd_case in options_test programme_test;do
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$vmd_sanitizer" -Isrc -I"$vmd_ffmpeg" \
  -I"$(xcrun --show-sdk-path)/usr/include/libxml2" tests/iptv/"$vmd_case".c \
  src/network/iptv_options.c src/network/hls_manifest.c src/network/dash_manifest.c \
  src/network/iptv_manifest.c src/network/iptv_catalog.c \
  "$vmd_ffmpeg/libavutil/libavutil.a" -lxml2 -lm -lpthread -lz -o "$vmd_output/options-test"
 if [ "$vmd_case" = programme_test ] && [ -n "${VMD_PUBLIC_EPG_FIXTURE:-}" ];then
  (cd "$vmd_output" && ./options-test "$VMD_PUBLIC_EPG_FIXTURE" ${VMD_PUBLIC_EPG_PLAYLIST:+"$VMD_PUBLIC_EPG_PLAYLIST"})
 else
  (cd "$vmd_output" && ./options-test)
 fi
 done
done
printf 'IPTV options host artifacts: %s\n' "$vmd_output"
