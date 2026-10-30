#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-volume-ui.XXXXXX")"
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/ui_crossfade/include -Itests/iptv/include -Itests/online_navigation/include \
 -Itests/online_audio/include "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cflags[@]}" \
 tests/audio_volume/ui_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/ui-test"
"$vmd_output/ui-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
printf 'Volume UI host previews: %s\n' "$vmd_output"
