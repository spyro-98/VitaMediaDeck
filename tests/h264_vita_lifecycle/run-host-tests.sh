#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_host="${VMD_HOST_FFMPEG:?Set the configured FFmpeg 6 host library/source root}"
vmd_archive="${VMD_FFMPEG_VITA_SOURCE:-$vmd_repo/build/deps/ffmpeg-vita-hw/share/sources/ffmpeg-n6.0-h264-vita-source.tar.gz}"
vmd_out="${VMD_H264_LIFECYCLE_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-h264-life.XXXXXX")}"
mkdir -p "$vmd_out/positive/libavcodec" "$vmd_out/negative/libavcodec"
tar -xOf "$vmd_archive" ffmpeg/libavcodec/vitadec_video.c > "$vmd_out/positive/libavcodec/vitadec_video.c"
cp "$vmd_out/positive/libavcodec/vitadec_video.c" "$vmd_out/negative/libavcodec/vitadec_video.c"
patch -R -p1 -d "$vmd_out/negative" < tools/patches/ffmpeg-vita-lifecycle.patch
for size in 64x64 128x96; do
 ffmpeg -y -v error -f lavfi -i "testsrc2=size=$size:rate=25" -frames:v 1 -c:v libx264 -preset ultrafast -bf 0 -refs 1 -f h264 "$vmd_out/$size.h264"
done
for mode in positive negative; do
 clang -O1 -g -Wall -Wextra -Werror -Wno-unknown-attributes -Wno-unused-function \
  -Wno-sign-compare -Wno-pointer-sign -fsanitize=address,undefined -ffunction-sections -fdata-sections \
  -Itests/h264_vita_lifecycle/include "-I$vmd_host" "-I$vmd_host/libavcodec" \
  "-DVMD_PINNED_VITA_DECODER_SOURCE=\"$vmd_out/$mode/libavcodec/vitadec_video.c\"" \
  tests/h264_vita_lifecycle/test.c "$vmd_host/libavcodec/libavcodec.a" "$vmd_host/libavutil/libavutil.a" \
  -Wl,-dead_strip -lpthread -lm -o "$vmd_out/$mode-test"
done
"$vmd_out/positive-test" "$vmd_out/64x64.h264" "$vmd_out/128x96.h264"
if "$vmd_out/negative-test" "$vmd_out/64x64.h264" "$vmd_out/128x96.h264" > "$vmd_out/negative.log" 2>&1; then
 echo 'Unpatched negative control unexpectedly passed' >&2; exit 1
else
 vmd_status=$?; [[ "$vmd_status" == 1 ]] || { cat "$vmd_out/negative.log"; exit 1; }
fi
printf 'Unpatched negative control: expected normal exit 1 (no crash).\n'
cat "$vmd_out/negative.log"
printf 'H264 lifecycle evidence: %s\n' "$vmd_out"
