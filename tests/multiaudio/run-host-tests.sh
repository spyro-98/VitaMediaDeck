#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_ffmpeg="${VMD_HOST_FFMPEG:?Configured FFmpeg 6 with AC3/MP3 decoders and swresample}"
vmd_out="${VMD_MULTIAUDIO_OUTPUT:-$(mktemp -d /private/tmp/vmd-multiaudio-test.XXXXXX)}"
mkdir -p "$vmd_out"
# Six-second 720p MKV with AAC, center-only 5.1 AC3 and 44.1-kHz MP3.
ffmpeg -y -loglevel error -f lavfi -i 'testsrc2=size=1280x720:rate=24' \
 -f lavfi -i 'sine=frequency=440:sample_rate=48000' \
 -f lavfi -i 'aevalsrc=0|0|0.3*sin(2*PI*660*t)|0|0|0:s=48000:c=5.1' \
 -f lavfi -i 'sine=frequency=880:sample_rate=44100' \
 -f lavfi -i 'sine=frequency=550:sample_rate=22050' \
 -f lavfi -i 'sine=frequency=330:sample_rate=11025' -t 6 \
 -map 0:v -map 1:a -map 2:a -map 3:a -map 4:a -map 5:a -c:v libx264 -preset ultrafast -g 24 -bf 2 -pix_fmt yuv420p \
 -c:a:0 aac -ac:a:0 2 -c:a:1 ac3 -b:a:1 384k -c:a:2 libmp3lame -ac:a:2 2 -c:a:3 libmp3lame -ac:a:3 1 -c:a:4 libmp3lame -ac:a:4 1 \
 -metadata:s:a:0 language=eng -metadata:s:a:1 language=ita -metadata:s:a:2 language=rus "$vmd_out/mixed.mkv"
vmd_libs=("$vmd_ffmpeg/libavformat/libavformat.a" "$vmd_ffmpeg/libavcodec/libavcodec.a" "$vmd_ffmpeg/libswresample/libswresample.a" "$vmd_ffmpeg/libavutil/libavutil.a" -framework CoreFoundation -framework Security -lz -lbz2 -liconv -lm -pthread -Wl,-dead_strip)
vmd_audio_includes=(-Isrc -I../vita-hw-audio-decoder/include -I../vita-sw-audio-decoder/include -I"$vmd_ffmpeg")
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined "${vmd_audio_includes[@]}" tests/multiaudio/formats_test.c "${vmd_libs[@]}" -o "$vmd_out/formats"
"$vmd_out/formats"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffunction-sections -fdata-sections "${vmd_audio_includes[@]}" -Itests/playback_seek/include -Itests/decoder/include tests/multiaudio/tracks_test.c "${vmd_libs[@]}" -o "$vmd_out/tracks"
"$vmd_out/tracks" "$vmd_out/mixed.mkv"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined "${vmd_audio_includes[@]}" tests/multiaudio/software_test.c ../vita-sw-audio-decoder/src/vita_sw_audio_decoder.c "${vmd_libs[@]}" -o "$vmd_out/software"
"$vmd_out/software" "$vmd_out/mixed.mkv"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/multiaudio/include -Itests/playback_seek/include -Itests/decoder/include "${vmd_audio_includes[@]}" tests/multiaudio/pipeline_test.c ../vita-sw-audio-decoder/src/vita_sw_audio_decoder.c ../vita-hw-audio-decoder/src/vita_hw_audio_decoder.c "${vmd_libs[@]}" -o "$vmd_out/pipeline"
"$vmd_out/pipeline" "$vmd_out/mixed.mkv"
for vmd_kind in hw sw;do
 vmd_defs=(-UTEST_HW);if [ "$vmd_kind" = hw ];then vmd_defs=(-DTEST_HW);fi
 clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffunction-sections -fdata-sections "${vmd_audio_includes[@]}" -Itests/multiaudio/include -Itests/playback_seek/include -Itests/decoder/include -I../vita-$vmd_kind-video-decoder/include -I../vita-$vmd_kind-video-decoder/src "${vmd_defs[@]}" tests/multiaudio/session_test.c ../vita-sw-audio-decoder/src/vita_sw_audio_decoder.c ../vita-hw-audio-decoder/src/vita_hw_audio_decoder.c "${vmd_libs[@]}" -o "$vmd_out/session-$vmd_kind"
 "$vmd_out/session-$vmd_kind" "$vmd_out/mixed.mkv"
done
printf 'Multicodec audio fixtures: %s\n' "$vmd_out"
