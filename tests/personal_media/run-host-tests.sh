#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_jansson="${VMD_HOST_JANSSON:?Set the host Jansson prefix}"
vmd_crypto="${VMD_HOST_CRYPTO:?Set the MbedTLS include directory}"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-personal-media.XXXXXX")"
vmd_flags=(-O1 -g -Wall -Wextra -Wno-deprecated-declarations -fsanitize=address,undefined -ffunction-sections -fdata-sections -Isrc -I../vita-https/include -I../vita-https/src -Itests/personal_media/include "-I$vmd_jansson/include" "-I$vmd_crypto")
read -r -a vmd_cflags <<<"$(pkg-config --cflags openssl libcurl)"
read -r -a vmd_libs <<<"$(pkg-config --libs openssl libcurl)"
vmd_link=("$vmd_jansson/lib/libjansson.a" "${vmd_libs[@]}" -Wl,-dead_strip)
clang "${vmd_flags[@]}" "${vmd_cflags[@]}" tests/personal_media/test.c src/network/personal_media.c "${vmd_link[@]}" -o "$vmd_output/api-test"
"$vmd_output/api-test"
for vmd_case in http-smoke range-test; do
 vmd_extra=(src/network/jellyfin_source.c)
 clang "${vmd_flags[@]}" "${vmd_cflags[@]}" "tests/personal_media/$vmd_case.c" src/network/personal_media.c "${vmd_extra[@]}" ../vita-https/src/vita_https.c ../vita-https/src/ca_bundle.c "${vmd_link[@]}" -o "$vmd_output/$vmd_case"
done
python3 tests/personal_media/http-server-test.py "$vmd_output"
read -r -a vmd_font_flags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_font_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections -Isrc -I../vita-https/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include -Itests/personal_media/include "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_font_flags[@]}" tests/personal_media/ui_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c "${vmd_font_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/ui-test"
"$vmd_output/ui-test" assets/fonts/Inter-Medium.ttf "$vmd_output"
printf 'Personal media host evidence: %s\n' "$vmd_output"
