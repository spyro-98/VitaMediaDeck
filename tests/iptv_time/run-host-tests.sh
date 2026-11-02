#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_output="${VMD_TIME_OUTPUT:-$(mktemp -d /private/tmp/vmd-iptv-time.XXXXXX)}"
mkdir -p "$vmd_output"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -Itests/iptv_time/include tests/iptv_time/test.c -o "$vmd_output/clock"
"$vmd_output/clock"
