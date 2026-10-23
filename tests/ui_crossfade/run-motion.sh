#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="${VMD_MOTION_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-motion-scenes.XXXXXX")}"
mkdir -p "$vmd_output"
read -r -a vmd_flags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/ui_crossfade/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_flags[@]}" \
 tests/ui_crossfade/motion_scene_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/motion-test"
"$vmd_output/motion-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
printf 'Native transition reference frames (host): %s\n' "$vmd_output"

clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/ui_crossfade/picker-include -Itests/ui_crossfade/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_flags[@]}" \
 tests/ui_crossfade/picker_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/picker-test"
"$vmd_output/picker-test" assets/fonts/Inter-Medium.ttf
