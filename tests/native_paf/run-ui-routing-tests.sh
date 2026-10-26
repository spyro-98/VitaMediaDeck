#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="${VMD_CHROME_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-ui-native-routing.XXXXXX")}"
mkdir -p "$vmd_output"
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/ui_chrome/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cflags[@]}" \
 tests/native_paf/ui-routing.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/native-routing-test"
"$vmd_output/native-routing-test" "$vmd_repo/assets/fonts/Inter-Medium.ttf" "$vmd_output"
printf 'Native routing fixtures: %s\n' "$vmd_output"
