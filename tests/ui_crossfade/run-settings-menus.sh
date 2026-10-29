#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_output="${VMD_MENU_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-settings-menu.XXXXXX")}"
mkdir -p "$vmd_output"
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cflags[@]}" \
 tests/ui_crossfade/settings_menu_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/menu-test"
"$vmd_output/menu-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
printf 'Menu reference frames (host): %s\n' "$vmd_output"
