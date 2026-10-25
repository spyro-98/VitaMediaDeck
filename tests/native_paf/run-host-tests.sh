#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-native-paf.XXXXXX")"
clang -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined \
 -Itests/native_paf/include -Isrc tests/native_paf/test.c -o "$vmd_output/native-paf-test"
"$vmd_output/native-paf-test"
