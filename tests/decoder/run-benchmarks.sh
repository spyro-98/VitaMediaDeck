#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_ffmpeg="${VMD_HOST_FFMPEG:?Set pinned host FFmpeg 6 source/build directory}"
vmd_output="${VMD_BENCH_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-bench.XXXXXX")}"
mkdir -p "$vmd_output"
clang -O3 -Wall -Wextra -I"$vmd_ffmpeg" tests/decoder/bench_ffmpeg.c \
 "$vmd_ffmpeg/libavformat/libavformat.a" "$vmd_ffmpeg/libavcodec/libavcodec.a" "$vmd_ffmpeg/libavutil/libavutil.a" \
 -framework CoreFoundation -framework Security -lz -lbz2 -liconv -lm -pthread -o "$vmd_output/bench"
for vmd_case in 640x360:25 854x480:30 1280x720:30 1280x720:60; do
 vmd_size="${vmd_case%:*}";vmd_fps="${vmd_case#*:}";vmd_name="${vmd_size}-${vmd_fps}"
 ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=$vmd_size:rate=$vmd_fps" -t 2 \
  -c:v libx264 -preset medium -crf 22 -pix_fmt yuv420p -x264-params 'bframes=3:ref=4:keyint=60' "$vmd_output/$vmd_name.mp4"
 "$vmd_output/bench" "$vmd_output/$vmd_name.mp4" > "$vmd_output/$vmd_name.csv"
done
printf 'Host decode CRC/thread benchmarks, not Vita energy: %s\n' "$vmd_output"
