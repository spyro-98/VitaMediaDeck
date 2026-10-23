#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-home-order.XXXXXX")"
for vmd_case in test persistence_test; do
 clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections \
  -Itests/home_tabs/include -Itests/online_audio/include -Isrc \
  "tests/home_tabs/$vmd_case.c" src/ui/sections_sidebar.c -Wl,-dead_strip -o "$vmd_output/$vmd_case"
 vmd_fixture="$(mktemp -d "$vmd_output/storage.XXXXXX")"
 (cd "$vmd_fixture"; "$vmd_output/$vmd_case")
done
