#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_ffmpeg="${VMD_HOST_FFMPEG:?Set the configured host FFmpeg source or include directory}"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-player-feedback.XXXXXX")"
vmd_flags=(-O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections -Isrc)
read -r -a vmd_codec_flags <<<"$(pkg-config --cflags libpng libjpeg libwebp)"
clang "${vmd_flags[@]}" -Itests/player_feedback/thumbnail/include \
    -Itests/podcast_metadata/include -Itests/online_audio/include "-I$vmd_ffmpeg" \
    "${vmd_codec_flags[@]}" tests/player_feedback/thumbnail/test.c -Wl,-dead_strip \
    $(pkg-config --libs libwebp) -o "$vmd_output/thumbnail-test"
"$vmd_output/thumbnail-test"
for vmd_test in seek mini; do
    clang "${vmd_flags[@]}" -DSCE_CTRL_HEADPHONE=8192 \
        -Itests/player_feedback/seek/include -Itests/player_feedback/thumbnail/include \
        -Itests/iptv/include -Itests/online_navigation/include \
        -Itests/online_audio/include "-I$vmd_ffmpeg" \
        "tests/player_feedback/$vmd_test/test.c" src/ui/theme.c -Wl,-dead_strip -o "$vmd_output/$vmd_test-test"
    "$vmd_output/$vmd_test-test"
done
printf 'Player feedback host test artifacts: %s\n' "$vmd_output"
