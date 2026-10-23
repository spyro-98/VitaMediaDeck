#!/usr/bin/env bash
set -euo pipefail
vmd_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_root"
vmd_jansson="${VMD_HOST_JANSSON:?Set a host Jansson prefix (include and lib)}"
vmd_flac="${VMD_HOST_FLAC:-/opt/homebrew/opt/flac}"
vmd_output="$(mktemp -d /tmp/vmd-podcast-metadata-check.XXXXXX)"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined \
  -Itests/podcast_metadata/include -Isrc -I"$vmd_jansson/include" -I"$vmd_flac/include" \
  tests/podcast_metadata/test.c src/media/podcast_metadata.c src/media/music_metadata.c \
  "$vmd_jansson/lib/libjansson.a" -L"$vmd_flac/lib" -lFLAC -o "$vmd_output/test"
"$vmd_output/test"
