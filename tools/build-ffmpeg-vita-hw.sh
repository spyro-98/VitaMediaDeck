#!/usr/bin/env bash
set -euo pipefail

# Reproducible builder for the complete public h264_vita backend used by
# wiliwili. Generated sources/objects stay under build/ and are not vendored.

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -z "${VITASDK:-}" ]]; then
  echo "VITASDK is not defined" >&2
  exit 1
fi
vita_sdk="$VITASDK"
prefix="${VITAMEDIADECK_H264_VITA_ROOT:-$repo_root/build/deps/ffmpeg-vita-hw}"
jobs="${VITAMEDIADECK_FFMPEG_JOBS:-8}"

ffmpeg_commit="ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2"
wiliwili_commit="88e5876bea9502d06f46a8656e3530684d3aaf7d"
patch_sha256="2d38529d10c74560db3909cf8c2f3e359b128c04fb29f6dd0085770ed81cef6c"

if [[ ! -x "$vita_sdk/bin/arm-vita-eabi-gcc" ]]; then
  echo "VitaSDK was not found at $vita_sdk" >&2
  exit 1
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/vitamediadeck-ffmpeg-hw.XXXXXX")"
trap 'rm -rf "$work"' EXIT

git clone --quiet https://github.com/FFmpeg/FFmpeg.git "$work/ffmpeg"
git -C "$work/ffmpeg" checkout --quiet "$ffmpeg_commit"
git clone --quiet https://github.com/xfangfang/wiliwili.git "$work/wiliwili"
git -C "$work/wiliwili" checkout --quiet "$wiliwili_commit"

patch_file="$work/wiliwili/scripts/psv/ffmpeg/ffmpeg.patch"
if command -v sha256sum >/dev/null 2>&1; then
  actual_sha="$(sha256sum "$patch_file" | awk '{print $1}')"
else
  actual_sha="$(shasum -a 256 "$patch_file" | awk '{print $1}')"
fi
if [[ "$actual_sha" != "$patch_sha256" ]]; then
  echo "Unexpected wiliwili patch SHA-256: $actual_sha" >&2
  exit 1
fi

patch -d "$work/ffmpeg" -p1 < "$patch_file"

# Preserve the exact patched tree before configuration adds generated files.
tar -C "$work" --exclude='ffmpeg/.git' -czf "$work/ffmpeg-n6.0-h264-vita-source.tar.gz" ffmpeg
git -C "$work/wiliwili" archive --format=tar.gz \
  --prefix=wiliwili-88e5876b/ -o "$work/wiliwili-88e5876b.tar.gz" HEAD

tool_prefix="$vita_sdk/bin/arm-vita-eabi-"
cd "$work/ffmpeg"
env VITASDK="$vita_sdk" PATH="$vita_sdk/bin:/usr/bin:/bin" ./configure \
  --prefix="$prefix" \
  --enable-vita --target-os=vita --enable-cross-compile \
  --cross-prefix="$tool_prefix" \
  --ar="${tool_prefix}gcc-ar" \
  --ranlib="${tool_prefix}gcc-ranlib" \
  --nm="${tool_prefix}gcc-nm" \
  --disable-runtime-cpudetect --disable-armv5te \
  --extra-cflags="-O3 -flto -ffunction-sections -fdata-sections -Wno-error=incompatible-pointer-types -Wno-error=enum-int-mismatch" \
  --extra-ldflags="-flto -Wl,--gc-sections -L$vita_sdk/lib" \
  --disable-shared --enable-static \
  --disable-programs --disable-doc --disable-autodetect --disable-network \
  --disable-avfilter --disable-swscale --disable-swresample --disable-avdevice \
  --disable-encoders \
  --disable-decoders --enable-decoder=h264,h264_vita \
  --disable-demuxers --enable-demuxer=mov,matroska,mpegts,aac,h264,srt \
  --disable-muxers --enable-muxer=mov,mp4,mpegts \
  --disable-parsers --enable-parser=aac,aac_latm,h264,mpeg4audio \
  --disable-protocols --enable-protocol=file \
  --disable-bsfs --disable-iconv --disable-lzma --disable-sdl2 --disable-xlib \
  --enable-pthreads

make -j"$jobs"
make install

# Keep the exact license text beside the generated static archives so the VPK
# can redistribute it without relying on a developer's temporary clone.
mkdir -p "$prefix/share/licenses" "$prefix/share/sources"
install -m 0644 "$work/ffmpeg/COPYING.LGPLv2.1" \
  "$prefix/share/licenses/FFmpeg-LGPL-2.1.txt"
install -m 0644 "$work/wiliwili/LICENSE" \
  "$prefix/share/licenses/wiliwili-GPL-3.0.txt"
install -m 0644 "$work/ffmpeg-n6.0-h264-vita-source.tar.gz" \
  "$prefix/share/sources/ffmpeg-n6.0-h264-vita-source.tar.gz"
install -m 0644 "$work/wiliwili-88e5876b.tar.gz" \
  "$prefix/share/sources/wiliwili-88e5876b.tar.gz"
install -m 0644 "$patch_file" \
  "$prefix/share/sources/wiliwili-ffmpeg.patch"

if ! "${tool_prefix}gcc-nm" -g --defined-only "$prefix/lib/libavcodec.a" |
     grep -q 'ff_h264_vita_decoder'; then
  echo "h264_vita is not registered in libavcodec.a" >&2
  exit 1
fi

echo "FFmpeg h264_vita is ready at $prefix"
