#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="${VMD_SCENE_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-scenes.XXXXXX")}"
mkdir -p "$vmd_output"
read -r -a vmd_flags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/ui_crossfade/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_flags[@]}" \
 tests/ui_crossfade/scene_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/scene-test"
"$vmd_output/scene-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
printf 'Native themed scenes (host): %s\n' "$vmd_output"
