#!/usr/bin/env bash
set -euo pipefail
vmd_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_root"
vmd_output="${VMD_LIBRARY_UI_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-library-ui.XXXXXX")}"
mkdir -p "$vmd_output"
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections -DSCE_O_APPEND=O_APPEND -Isrc -Itests/podcast_resume/include -Itests/podcast_metadata/include -c src/library/library.c -o "$vmd_output/library.o"
for vmd_case in ${VMD_LIBRARY_UI_CASES:-ui_test home_refresh_test};do
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cflags[@]}" \
 tests/library/"$vmd_case".c src/ui/home_dashboard.c src/library/catalog.c "$vmd_output/library.o" src/network/iptv_manifest.c src/network/iptv_catalog.c \
 -Itests/podcast_resume/include -Itests/podcast_metadata/include "${vmd_libs[@]}" -lxml2 -lpthread -Wl,-dead_strip -o "$vmd_output/$vmd_case"
"$vmd_output/$vmd_case" assets/fonts/Inter-Medium.ttf "$vmd_output"
done
printf 'Unified Home host layouts: %s\n' "$vmd_output"
