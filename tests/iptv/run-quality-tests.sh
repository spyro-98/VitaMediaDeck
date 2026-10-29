#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_jansson="${VMD_HOST_JANSSON:?Host Jansson prefix}"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-quality-tests.XXXXXX")"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -I"$vmd_jansson/include" \
 tests/iptv/quality_test.c src/network/iptv_quality.c "$vmd_jansson/lib/libjansson.a" -lm -o "$vmd_output/quality-test"
"$vmd_output/quality-test"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -I"$vmd_jansson/include" \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" \
 tests/iptv/saved_test.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
 "$vmd_jansson/lib/libjansson.a" -lxml2 -lm -o "$vmd_output/saved-test"
"$vmd_output/saved-test"
