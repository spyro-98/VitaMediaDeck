#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_ffmpeg="${VMD_HOST_FFMPEG:?Set pinned host FFmpeg 6 source/build directory}"
vmd_output="${VMD_DECODER_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-decoder.XXXXXX")}"
mkdir -p "$vmd_output"
ffmpeg -y -loglevel error -f lavfi -i testsrc2=size=64x64:rate=30 -frames:v 6 -c:v libx264 -pix_fmt yuv420p "$vmd_output/fixture.mp4"
for vmd_mode in hw sw planar; do
 vmd_defines=(-DVITA_HW_DECODER_ALLOW_SOFTWARE_FALLBACK=1 -DVITA_SW_DECODER_ALLOW_SOFTWARE_FALLBACK=1)
 if [[ "$vmd_mode" == hw ]]; then
  vmd_package=../vita-hw-decoder
  vmd_defines+=(-DTEST_HW -DVITA_HW_DECODER_HAS_H264_VITA=1)
 else
  vmd_package=../vita-sw-decoder
  vmd_defines+=(-DVITA_SW_DECODER_HAS_H264_VITA=0)
  if [[ "$vmd_mode" == planar ]]; then vmd_defines+=(-DSOFTWARE_VIDEO_PLANAR_COPY_ENABLED=1); fi
 fi
 clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections \
  -Itests/decoder/include -I"$vmd_package/src" -I"$vmd_ffmpeg" "${vmd_defines[@]}" \
  tests/decoder/test.c "$vmd_ffmpeg/libavformat/libavformat.a" "$vmd_ffmpeg/libavcodec/libavcodec.a" "$vmd_ffmpeg/libavutil/libavutil.a" \
  -framework CoreFoundation -framework Security -lz -lbz2 -liconv -lm -pthread -Wl,-dead_strip -o "$vmd_output/test-$vmd_mode"
 "$vmd_output/test-$vmd_mode" "$vmd_output/fixture.mp4"
done
printf 'Decoder checks (host, not Vita HW/performance): %s\n' "$vmd_output"
for vmd_profile in performance stock; do
 vmd_options=(-UVT_VIDEO_STOCK_CLOCKS)
 if [[ "$vmd_profile" == stock ]]; then vmd_options+=(-DVT_VIDEO_STOCK_CLOCKS=1); fi
 clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc -Itests/decoder/include "${vmd_options[@]}" tests/decoder/clocks_test.c -o "$vmd_output/clocks-$vmd_profile"
 "$vmd_output/clocks-$vmd_profile"
done
