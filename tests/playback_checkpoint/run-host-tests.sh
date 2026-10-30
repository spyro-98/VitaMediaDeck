#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_out="$(mktemp -d "${TMPDIR:-/tmp}/vmd-playback-checkpoint.XXXXXX")"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Isrc -Itests/playback_checkpoint/include -Itests/podcast_resume/include \
 tests/playback_checkpoint/test.c -o "$vmd_out/test"
"$vmd_out/test"
