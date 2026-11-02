#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_out="$(mktemp -d "${TMPDIR:-/tmp}/vmd-volume.XXXXXX")"
python3 tests/audio_volume/extract-output.py "$vmd_out"
for vmd_sanitizer in address,undefined thread; do
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$vmd_sanitizer" \
  -Isrc -I../vita-hw-audio-decoder/include -I"$vmd_out" tests/audio_volume/gain_test.c src/media/audio_volume.c -lm -o "$vmd_out/gain"
 "$vmd_out/gain"
 clang -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize="$vmd_sanitizer" \
  -Isrc -I../vita-hw-audio-decoder/include -I"$vmd_out" tests/audio_volume/output_test.c -o "$vmd_out/output"
 "$vmd_out/output"
done
printf 'Audio gain evidence: %s\n' "$vmd_out"
