#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-iptv-check.XXXXXX")"
vmd_sdk="$(xcrun --show-sdk-path)"
vmd_flags=(-O1 -g -Wall -Wextra -Isrc "-I$vmd_sdk/usr/include/libxml2")
vmd_sources=(src/network/iptv_manifest.c src/network/iptv_source.c)
for vmd_sanitizer in address,undefined thread; do
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" -I../vita-https/include \
        tests/iptv/test.c "${vmd_sources[@]}" src/network/iptv_http.c \
        -lxml2 -lpthread -o "$vmd_output/source-test"
    "$vmd_output/source-test"
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/iptv/session_test.c \
        "${vmd_sources[@]}" src/network/iptv_session.c -lxml2 -lpthread \
        -o "$vmd_output/session-test"
    "$vmd_output/session-test"
done
read -r -a vmd_font_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_font_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang "${vmd_flags[@]}" -fsanitize=address,undefined -ffunction-sections -fdata-sections \
    -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
    "${vmd_font_cflags[@]}" tests/iptv/ui_test.c src/network/iptv_manifest.c \
    "${vmd_font_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/ui-test"
"$vmd_output/ui-test" assets/fonts/Inter-Medium.ttf "$vmd_output"

if [[ -n "${VMD_IPTV_FFMPEG_ROOT:-}" && -n "${VMD_IPTV_FIXTURE:-}" ]]; then
    clang "${vmd_flags[@]}" -fsanitize=address,undefined "-I$VMD_IPTV_FFMPEG_ROOT" \
        tests/iptv/demux_test.c "${vmd_sources[@]}" src/network/iptv_session.c \
        "$VMD_IPTV_FFMPEG_ROOT/libavformat/libavformat.a" \
        "$VMD_IPTV_FFMPEG_ROOT/libavcodec/libavcodec.a" \
        "$VMD_IPTV_FFMPEG_ROOT/libavutil/libavutil.a" \
        -lxml2 -lpthread -lm -o "$vmd_output/demux-test"
    "$vmd_output/demux-test" "$VMD_IPTV_FIXTURE"
fi
printf 'Host test artifacts (not Vita screenshots): %s\n' "$vmd_output"
