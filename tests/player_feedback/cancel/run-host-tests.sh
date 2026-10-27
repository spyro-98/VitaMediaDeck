#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-iptv-cancel.XXXXXX")"
for vmd_sanitizer in address,undefined thread; do
 clang -O1 -g -Wall -Wextra "-fsanitize=$vmd_sanitizer" -ffunction-sections -fdata-sections -DSCE_CTRL_HEADPHONE=8192 -Isrc \
  -Itests/player_feedback/seek/include -Itests/player_feedback/thumbnail/include -Itests/iptv/include \
  -Itests/online_navigation/include -Itests/online_audio/include "-I${VMD_HOST_FFMPEG:?Set host FFmpeg include root}" \
  "-I$(xcrun --show-sdk-path)/usr/include/libxml2" tests/player_feedback/cancel/test.c \
  src/network/iptv_source.c src/network/iptv_session.c src/network/iptv_manifest.c -lxml2 -lpthread -Wl,-dead_strip -o "$vmd_output/test"
 "$vmd_output/test"
done
