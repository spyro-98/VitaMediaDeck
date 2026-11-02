#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${VITASDK:?Set VITASDK}"
vmd_root="$(pwd)"
vmd_output="${VMD_PACKAGE_OUTPUT:-$vmd_root/build/package-validation}"
vmd_ffmpeg="${VMD_PACKAGE_FFMPEG:-$vmd_root/build/deps/ffmpeg-vita-hw}"
mkdir -p "$vmd_output"
for vmd_kind in hw-video sw-video hw-audio sw-audio;do
 vmd_var="$(printf '%s' "$vmd_kind" | tr '[:lower:]-' '[:upper:]_')"
 cmake -S "../vita-$vmd_kind-decoder" -B "$vmd_output/$vmd_kind" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$vmd_output/install" \
  "-DCMAKE_C_FLAGS=${VMD_PACKAGE_C_FLAGS:-}" \
  "-DVITA_${vmd_var}_DECODER_FFMPEG_ROOT=$vmd_ffmpeg" \
  "-DVITA_${vmd_var}_DECODER_BUILD_EXAMPLE=ON"
 cmake --build "$vmd_output/$vmd_kind" --parallel 4
 cmake --install "$vmd_output/$vmd_kind"
done
# An installed consumer must resolve all exported targets and the interface,
# without source-tree include paths. No unneeded native audio/video backends
# are pulled into any of the separate static archives.
mkdir -p "$vmd_output/consumer"
cat > "$vmd_output/consumer/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(installed_decoder_consumer C)
find_package(VitaHwVideoDecoder 3 CONFIG REQUIRED)
find_package(VitaSwVideoDecoder 3 CONFIG REQUIRED)
find_package(VitaHwAudioDecoder 1 CONFIG REQUIRED)
find_package(VitaSwAudioDecoder 1 CONFIG REQUIRED)
add_library(consumer OBJECT consumer.c)
target_link_libraries(consumer PRIVATE VitaHwVideoDecoder::VitaHwVideoDecoder VitaSwVideoDecoder::VitaSwVideoDecoder VitaHwAudioDecoder::VitaHwAudioDecoder VitaSwAudioDecoder::VitaSwAudioDecoder)
CMAKE
cat > "$vmd_output/consumer/consumer.c" <<'C'
#include <vita_hw_video_decoder.h>
#include <vita_sw_video_decoder.h>
#include <vita_hw_audio_decoder.h>
#include <vita_sw_audio_decoder.h>
void verify_public_headers(void) {
 VitaHwVideoDecoderPlayerConfig hw={0};VitaSwVideoDecoderPlayerConfig sw={0};
 VitaHwAudioDecoderConfig audio={.codec=VITA_AUDIO_CODEC_AAC,.channels=2,.sample_rate=48000};
 (void)hw;(void)sw;(void)audio;
}
C
cmake -S "$vmd_output/consumer" -B "$vmd_output/consumer-build" \
 "-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake" \
 "-DCMAKE_PREFIX_PATH=$vmd_output/install" \
 "-DCMAKE_INSTALL_PREFIX=$vmd_output/install" \
 "-DCMAKE_C_FLAGS=${VMD_PACKAGE_C_FLAGS:-} -I$vmd_ffmpeg/include"
cmake --build "$vmd_output/consumer-build" --parallel 2
python3 tests/decoder_packages/audit.py --build build/native
