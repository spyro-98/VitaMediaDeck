#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_out="$(mktemp -d /private/tmp/vmd-playback-persistence.XXXXXX)"
read -r -a vmd_cf <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_lf <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isrc -I../vita-https/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
 -I"${VMD_HOST_JANSSON:?Host Jansson prefix}/include" "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cf[@]}" \
 tests/iptv/playback_restart_test.c src/network/iptv_quality.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "$VMD_HOST_JANSSON/lib/libjansson.a" "${vmd_lf[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_out/test"
"$vmd_out/test"
