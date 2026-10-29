#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-navigation.XXXXXX")"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -Itests/iptv/include \
 tests/navigation/test.c -o "$vmd_output/navigation-test"
"$vmd_output/navigation-test"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -Itests/iptv/include \
 tests/navigation/scroll_test.c -lm -o "$vmd_output/scroll-test"
"$vmd_output/scroll-test"
