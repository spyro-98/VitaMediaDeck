#!/usr/bin/env bash
set -euo pipefail
vmd_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_root"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-podcast-resume-check.XXXXXX")"
python3 tests/podcast_resume/extract-mp3.py "$vmd_output/play-mp3.inc"
read -r -a mp3_flags <<<"$(pkg-config --cflags libmpg123)"
read -r -a mp3_libs <<<"$(pkg-config --libs libmpg123)"
for variant in cbr vbr; do
 if [[ "$variant" == cbr ]]; then codec_args=(-b:a 128k); else codec_args=(-q:a 3); fi
 ffmpeg -hide_banner -loglevel error -f lavfi -i sine=frequency=440:sample_rate=44100 -t 60 -ac 2 -c:a libmp3lame "${codec_args[@]}" "$vmd_output/$variant.mp3"
done
for sanitizer in address,undefined thread; do
 clang -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize="$sanitizer" \
  -Isrc -Itests/podcast_resume/include -Itests/podcast_metadata/include \
  tests/podcast_resume/test.c -lpthread -o "$vmd_output/test"
 "$vmd_output/test"
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$sanitizer" -Isrc -I"$vmd_output" "${mp3_flags[@]}" \
  tests/podcast_resume/mp3_test.c "${mp3_libs[@]}" -o "$vmd_output/mp3-test"
 "$vmd_output/mp3-test" "$vmd_output/cbr.mp3" "$vmd_output/vbr.mp3"
done
printf 'Podcast resume host artifacts: %s\n' "$vmd_output"
