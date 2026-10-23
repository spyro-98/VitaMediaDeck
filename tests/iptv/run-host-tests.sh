#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-iptv-check.XXXXXX")"
vmd_sdk="$(xcrun --show-sdk-path)"
vmd_flags=(-O1 -g -Wall -Wextra -Isrc -I../vita-https/include "-I$vmd_sdk/usr/include/libxml2")
vmd_sources=(src/network/iptv_manifest.c src/network/iptv_source.c)
if [[ -n "${VMD_JANSSON_HOST_ROOT:-}" ]]; then
    clang "${vmd_flags[@]}" -fsanitize=address,undefined "-I$VMD_JANSSON_HOST_ROOT/include" \
        tests/iptv/saved_test.c src/network/iptv_catalog.c src/network/iptv_manifest.c \
        "$VMD_JANSSON_HOST_ROOT/lib/libjansson.a" -lxml2 -o "$vmd_output/saved-test"
    if [[ -n "${VMD_IPTV_CATALOG_FIXTURE:-}" ]]; then
        "$vmd_output/saved-test" "$VMD_IPTV_CATALOG_FIXTURE"
    else
        "$vmd_output/saved-test"
    fi
else
    printf 'Saved-entry persistence test requires VMD_JANSSON_HOST_ROOT (host Jansson prefix).\n'
fi
python3 tools/pack-flags.py assets/flags "$vmd_output/flags.pack"
clang "${vmd_flags[@]}" -fsanitize=address,undefined tests/iptv/artwork_test.c -o "$vmd_output/artwork-test"
"$vmd_output/artwork-test" "$vmd_output/flags.pack" assets/flags
for vmd_sanitizer in address,undefined thread; do
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/iptv/probe_test.c \
        src/network/iptv_probe.c src/network/iptv_catalog.c src/network/iptv_manifest.c \
        -lxml2 -o "$vmd_output/probe-test"
    "$vmd_output/probe-test"
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" -I../vita-https/include \
        tests/iptv/test.c "${vmd_sources[@]}" src/network/iptv_http.c \
        -lxml2 -lpthread -o "$vmd_output/source-test"
    "$vmd_output/source-test"
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/iptv/session_test.c \
        "${vmd_sources[@]}" src/network/iptv_session.c -lxml2 -lpthread \
        -o "$vmd_output/session-test"
    "$vmd_output/session-test"
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/iptv/vod_test.c \
        "${vmd_sources[@]}" -lxml2 -lpthread -o "$vmd_output/vod-test"
    "$vmd_output/vod-test"
done
read -r -a vmd_font_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_font_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang "${vmd_flags[@]}" -fsanitize=address,undefined -ffunction-sections -fdata-sections \
    -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
    "${vmd_font_cflags[@]}" tests/iptv/ui_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
    "${vmd_font_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/ui-test"
"$vmd_output/ui-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
clang "${vmd_flags[@]}" -fsanitize=address,undefined -ffunction-sections -fdata-sections \
    -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
    "${vmd_font_cflags[@]}" tests/iptv/catalog_ui_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
    "${vmd_font_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/catalog-ui-test"
"$vmd_output/catalog-ui-test" assets/fonts/Inter-Medium.ttf "$vmd_output"

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
