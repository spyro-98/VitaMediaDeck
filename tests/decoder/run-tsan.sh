#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_ffmpeg="${VMD_HOST_FFMPEG:?Set pinned host FFmpeg 6 source/build directory}"
vmd_output="${VMD_DECODER_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-decoder-tsan.XXXXXX")}"
mkdir -p "$vmd_output"
clang -O1 -g -Wall -Wextra -fsanitize=thread -ffunction-sections -fdata-sections \
 -Itests/decoder/include -I../vita-sw-decoder/src -I"$vmd_ffmpeg" -DVITA_SW_DECODER_HAS_H264_VITA=0 -DVITA_SW_DECODER_ALLOW_SOFTWARE_FALLBACK=1 \
 tests/decoder/test.c "$vmd_ffmpeg/libavformat/libavformat.a" "$vmd_ffmpeg/libavcodec/libavcodec.a" "$vmd_ffmpeg/libavutil/libavutil.a" \
 -framework CoreFoundation -framework Security -lz -lbz2 -liconv -lm -pthread -Wl,-dead_strip -o "$vmd_output/waits-tsan"
VMD_WAIT_ONLY=1 "$vmd_output/waits-tsan" ignored
