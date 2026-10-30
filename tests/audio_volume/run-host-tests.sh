#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_out="$(mktemp -d "${TMPDIR:-/tmp}/vmd-volume.XXXXXX")"
# Each decoder is independently buildable; guard the common gain contract.
cmp src/media/audio_gain.h ../vita-hw-decoder/src/internal/audio_gain.h
cmp src/media/audio_gain.h ../vita-sw-decoder/src/internal/audio_gain.h
python3 tests/audio_volume/extract-output.py "$vmd_out"
for vmd_sanitizer in address,undefined thread; do
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$vmd_sanitizer" \
  -Isrc -I"$vmd_out" tests/audio_volume/gain_test.c src/media/audio_volume.c -lm -o "$vmd_out/gain"
 "$vmd_out/gain"
 for vmd_backend in hw sw; do
  vmd_flags=(-UTEST_HW);if [[ "$vmd_backend" == hw ]]; then vmd_flags+=(-DTEST_HW);fi
  clang -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize="$vmd_sanitizer" \
   -Isrc -I"$vmd_out" "${vmd_flags[@]}" tests/audio_volume/output_test.c -o "$vmd_out/output-$vmd_backend"
  "$vmd_out/output-$vmd_backend"
 done
done
printf 'Audio gain evidence: %s\n' "$vmd_out"
