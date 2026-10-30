#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_jansson="${VMD_HOST_JANSSON:?Host Jansson prefix}"
vmd_out=$(mktemp -d /private/tmp/vmd-jellyfin.XXXXXX)
for vmd_sanitizer in address,undefined thread;do
 clang -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize="$vmd_sanitizer" -ffunction-sections -fdata-sections \
  -Isrc -I../vita-https/include -Itests/player_feedback/thumbnail/include -Itests/podcast_metadata/include \
  -I"$vmd_jansson/include" tests/jellyfin/playback_test.c src/network/iptv_manifest.c \
  "$vmd_jansson/lib/libjansson.a" -lxml2 -Wl,-dead_strip -o "$vmd_out/test"
 "$vmd_out/test"
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$vmd_sanitizer" -Isrc -I../vita-https/include \
  -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
  tests/jellyfin/player_test.c -o "$vmd_out/player-test"
 "$vmd_out/player-test"
done
printf 'Jellyfin host evidence: %s\n' "$vmd_out"
