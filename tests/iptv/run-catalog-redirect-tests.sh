#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_dir="$(mktemp -d "${TMPDIR:-/tmp}/vmd-catalog-redirect.XXXXXX")"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc \
 "-I$(xcrun --show-sdk-path)/usr/include/libxml2" tests/iptv/catalog_redirect_test.c \
 src/network/iptv_catalog.c src/network/iptv_manifest.c -lxml2 -o "$vmd_dir/test"
"$vmd_dir/test" "$@"
