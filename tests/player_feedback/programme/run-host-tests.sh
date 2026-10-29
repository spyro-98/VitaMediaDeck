#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
vmd_out=$(mktemp -d /private/tmp/vmd-programme-async.XXXXXX)
for vmd_sanitizer in address,undefined thread;do
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$vmd_sanitizer" -ffunction-sections -fdata-sections -DSCE_CTRL_HEADPHONE=8192 \
  -Isrc -Itests/player_feedback/seek/include -Itests/player_feedback/thumbnail/include \
  -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include \
  -I"${VMD_HOST_FFMPEG:?Configured host FFmpeg source}" \
  tests/player_feedback/programme/test.c src/media/iptv_program_async.c -lpthread -Wl,-dead_strip -o "$vmd_out/test"
 "$vmd_out/test"
done
printf 'Asynchronous programme worker evidence: %s\n' "$vmd_out"
