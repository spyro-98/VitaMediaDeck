#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-theme-flow.XXXXXX")"
vmd_flags=(-O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections -Isrc -I../vita-https/include)
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
for vmd_case in catalog_ui_test theme_flow_test cache_transition_test; do
 clang "${vmd_flags[@]}" -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
   "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cflags[@]}" \
   "tests/iptv/$vmd_case.c" src/network/iptv_manifest.c src/network/iptv_catalog.c \
   "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/$vmd_case"
 "$vmd_output/$vmd_case" assets/fonts/Inter-Medium.ttf "$vmd_output"
done
printf 'Native theme flow screenshots (host): %s\n' "$vmd_output"
