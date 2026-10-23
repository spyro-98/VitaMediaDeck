#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-crossfade-check.XXXXXX")"
vmd_flags=(-O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc -I../vita-https/include)
clang "${vmd_flags[@]}" tests/ui_crossfade/page_motion_test.c -o "$vmd_output/page-motion-test"
"$vmd_output/page-motion-test"
clang "${vmd_flags[@]}" -Itests/ui_crossfade/include -Itests/online_audio/include \
    tests/ui_crossfade/focus_test.c src/ui/focus_glow.c src/ui/theme.c -o "$vmd_output/focus-test"
"$vmd_output/focus-test"
clang "${vmd_flags[@]}" -Itests/podcast_metadata/include \
    tests/ui_crossfade/preferences_test.c -o "$vmd_output/preferences-test"
"$vmd_output/preferences-test"
clang "${vmd_flags[@]}" tests/ui_crossfade/local_summary_test.c -o "$vmd_output/local-summary-test"
"$vmd_output/local-summary-test"
read -r -a vmd_font_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_font_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang "${vmd_flags[@]}" -ffunction-sections -fdata-sections \
    -Itests/ui_crossfade/include -Itests/iptv/include -Itests/online_navigation/include \
    -Itests/online_audio/include "-I$(xcrun --show-sdk-path)/usr/include/libxml2" \
    "${vmd_font_cflags[@]}" tests/ui_crossfade/audio_test.c src/network/iptv_manifest.c \
    src/network/iptv_catalog.c "${vmd_font_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/audio-test"
"$vmd_output/audio-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
clang "${vmd_flags[@]}" -ffunction-sections -fdata-sections \
    -Itests/ui_crossfade/include -Itests/iptv/include -Itests/online_navigation/include \
    -Itests/online_audio/include "-I$(xcrun --show-sdk-path)/usr/include/libxml2" \
    "${vmd_font_cflags[@]}" tests/ui_crossfade/pages_test.c src/network/iptv_manifest.c \
    src/network/iptv_catalog.c "${vmd_font_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/pages-test"
"$vmd_output/pages-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
clang "${vmd_flags[@]}" -ffunction-sections -fdata-sections \
    -Itests/ui_crossfade/include -Itests/iptv/include -Itests/online_navigation/include \
    -Itests/online_audio/include "-I$(xcrun --show-sdk-path)/usr/include/libxml2" \
    "${vmd_font_cflags[@]}" tests/ui_crossfade/download_test.c src/network/iptv_manifest.c \
    src/network/iptv_catalog.c "${vmd_font_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/download-test"
"$vmd_output/download-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
clang "${vmd_flags[@]}" -ffunction-sections -fdata-sections \
    -Itests/ui_crossfade/include -Itests/iptv/include -Itests/online_navigation/include \
    -Itests/online_audio/include "-I$(xcrun --show-sdk-path)/usr/include/libxml2" \
    "${vmd_font_cflags[@]}" tests/ui_crossfade/home_test.c src/network/iptv_manifest.c \
    src/network/iptv_catalog.c "${vmd_font_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/home-test"
"$vmd_output/home-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
printf 'Host-rendered native layouts (not Vita screenshots): %s\n' "$vmd_output"
