#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_ffmpeg="${VMD_HOST_FFMPEG:?Set pinned host FFmpeg 6 source/build directory}"
vmd_output="${VMD_PLAYBACK_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-playback-seek.XXXXXX")}"
mkdir -p "$vmd_output"
python3 - "$vmd_output" <<'PY'
import sys
from pathlib import Path
out=Path(sys.argv[1])
(out/'cues.srt').write_text(''.join(f'{n+1}\n00:00:{n:02d},000 --> 00:00:{n:02d},800\ncue-{n}\n\n' for n in range(24)))
PY
ffmpeg -y -loglevel error -f lavfi -i testsrc2=size=320x180:rate=10 -f lavfi -i sine=frequency=440 -i "$vmd_output/cues.srt" -t 24 -map 0:v -map 1:a -map 2 -c:v libx264 -crf 16 -g 20 -bf 2 -pix_fmt yuv420p -c:a aac -c:s mov_text "$vmd_output/tail-moov.mp4"
ffmpeg -y -loglevel error -i "$vmd_output/tail-moov.mp4" -map 0 -c copy -movflags +faststart "$vmd_output/faststart.mp4"
ffmpeg -y -loglevel error -i "$vmd_output/tail-moov.mp4" -c:v copy -c:a copy -c:s srt "$vmd_output/subtitles.mkv"
ffmpeg -y -loglevel error -i "$vmd_output/tail-moov.mp4" -map 0 -c copy -movflags +frag_keyframe+empty_moov+default_base_moof "$vmd_output/fragmented.mp4"
python3 - "$vmd_output" <<'PY2'
from pathlib import Path
import sys
p=Path(sys.argv[1])
(p/'english.srt').write_text((p/'cues.srt').read_text().replace('cue-','ENG cue-'))
PY2
ffmpeg -y -loglevel error -i "$vmd_output/tail-moov.mp4" -f lavfi -i sine=frequency=880 -i "$vmd_output/english.srt" -t 24 -map 0:v -map 0:a -map 1:a -map 0:s -map 2:s -c:v copy -c:a aac -c:s mov_text -metadata:s:a:0 language=ita -metadata:s:a:1 language=eng -metadata:s:s:0 language=ita -metadata:s:s:1 language=eng -metadata:s:a:0 title="Test 440 Hz" -metadata:s:a:1 title="Test 880 Hz" -movflags +faststart "$vmd_output/multi-track.mp4"
vmd_libs=("$vmd_ffmpeg/libavformat/libavformat.a" "$vmd_ffmpeg/libavcodec/libavcodec.a" "$vmd_ffmpeg/libavutil/libavutil.a" -framework CoreFoundation -framework Security -lz -lbz2 -liconv -lm -pthread -Wl,-dead_strip)
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections -Isrc -Itests/playback_seek/include -Itests/decoder/include -I"$vmd_ffmpeg" tests/playback_seek/subtitles.c "${vmd_libs[@]}" -o "$vmd_output/subtitles"
"$vmd_output/subtitles" "$vmd_output/subtitles.mkv" "$vmd_output/tail-moov.mp4" "$vmd_output/faststart.mp4" "$vmd_output/fragmented.mp4" "$vmd_output/multi-track.mp4"
for vmd_backend in hw sw; do
 vmd_defs=(-UTEST_SW)
 if [[ "$vmd_backend" == sw ]]; then vmd_defs+=(-DTEST_SW); fi
 clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$vmd_ffmpeg" "${vmd_defs[@]}" tests/playback_seek/duration.c "${vmd_libs[@]}" -o "$vmd_output/duration-$vmd_backend"
 "$vmd_output/duration-$vmd_backend" "$vmd_output/subtitles.mkv" "$vmd_output/tail-moov.mp4" "$vmd_output/faststart.mp4" "$vmd_output/fragmented.mp4" "$vmd_output/multi-track.mp4"
done
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections -Isrc -Itests/playback_seek/include -Itests/decoder/include -Itests/iptv/include -Itests/podcast_metadata/include tests/playback_seek/logging.c -pthread -Wl,-dead_strip -o "$vmd_output/logging"
"$vmd_output/logging"
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -ffunction-sections -fdata-sections -Isrc -I../vita-hw-decoder/include -I../vita-sw-decoder/include -Itests/playback_seek/include -Itests/decoder/include -Itests/podcast_metadata/include -I"$vmd_ffmpeg" tests/playback_seek/adapter.c -Wl,-dead_strip -o "$vmd_output/adapter"
"$vmd_output/adapter"
printf 'Playback host artifacts: %s\n' "$vmd_output"
