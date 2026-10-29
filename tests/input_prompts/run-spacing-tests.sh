#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_output="${VMD_SPACING_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-controller-spacing.XXXXXX")}"
mkdir -p "$vmd_output"
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined \
 -ffunction-sections -fdata-sections -Isrc -I../vita-https/include -Itests/ui_chrome/include -Itests/ui_crossfade/include \
 -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cflags[@]}" \
 tests/input_prompts/spacing.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/spacing-test"
"$vmd_output/spacing-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
printf 'Host spacing previews: %s\n' "$vmd_output"
