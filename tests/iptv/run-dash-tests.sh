#!/usr/bin/env bash
set -euo pipefail
vmd_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_root"
vmd_ffmpeg="${VMD_IPTV_FFMPEG_ROOT:?Set the host FFmpeg 6 library/source directory}"
vmd_fixture="${VMD_DASH_FIXTURE:?Set the generated 12-second DASH fixture directory}"
vmd_sdk="$(xcrun --show-sdk-path)"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-dash-check.XXXXXX")"
vmd_flags=(-O1 -g -Wall -Wextra -Isrc "-I$vmd_ffmpeg" "-I$vmd_sdk/usr/include/libxml2")
vmd_sources=(src/network/dash_manifest.c src/network/dash_source.c src/network/iptv_manifest.c src/network/iptv_source.c src/network/iptv_session.c)
vmd_libs=("$vmd_ffmpeg/libavformat/libavformat.a" "$vmd_ffmpeg/libavcodec/libavcodec.a" "$vmd_ffmpeg/libavutil/libavutil.a" -lxml2 -lpthread -lm)
for vmd_sanitizer in address,undefined thread; do
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/iptv/dash_manifest_test.c \
        src/network/dash_manifest.c src/network/iptv_manifest.c "${vmd_libs[@]}" -o "$vmd_output/manifest-test"
    "$vmd_output/manifest-test"
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/iptv/dash_demux_test.c \
        "${vmd_sources[@]}" "${vmd_libs[@]}" -o "$vmd_output/demux-test"
    "$vmd_output/demux-test" "$vmd_fixture"
    "$vmd_output/demux-test" "$vmd_fixture" shift
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/iptv/dash_lifecycle_test.c \
        "${vmd_sources[@]}" "${vmd_libs[@]}" -o "$vmd_output/lifecycle-test"
    "$vmd_output/lifecycle-test" "$vmd_fixture"
done
printf 'DASH host test artifacts: %s\n' "$vmd_output"
