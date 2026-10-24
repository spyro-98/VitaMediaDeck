#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="${VMD_GAME_UI_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-game-ui.XXXXXX")}"
mkdir -p "$vmd_output"
printf '#include "ui/home_services.h"\n' | clang -Wall -Wextra -Werror -Isrc -x c -fsyntax-only -
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
for vmd_case in game_ui sidebar_scroll; do
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cflags[@]}" \
 tests/ui_crossfade/${vmd_case}_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/${vmd_case}-test"
"$vmd_output/${vmd_case}-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
done
printf 'Native host rendering: %s\n' "$vmd_output"
