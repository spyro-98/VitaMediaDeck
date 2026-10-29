#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-input-prompts.XXXXXX")"
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -DVMD_ENABLE_PAF_NATIVE=1 -Isrc -Itests/input_prompts/include "${vmd_cflags[@]}" \
 tests/input_prompts/test.c src/ui/font.c src/ui/input_prompts.c "${vmd_libs[@]}" \
 -o "$vmd_output/input-prompts-test"
"$vmd_output/input-prompts-test" assets/fonts/Inter-Medium.ttf assets/input-prompts
"$vmd_output/input-prompts-test" assets/fonts/IBMPlexMono-Regular.ttf assets/input-prompts
