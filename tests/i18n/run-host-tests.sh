#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-languages.XXXXXX")"
python3 tests/i18n/check-catalogs.py
for vmd_case in registry_test catalog_test;do
 clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc \
  -Itests/input_prompts/include tests/i18n/$vmd_case.c -o "$vmd_output/$vmd_case"
 "$vmd_output/$vmd_case"
done
clang -O1 -g -Wall -Wextra -fsanitize=address,undefined -Isrc \
 -Itests/podcast_metadata/include tests/ui_crossfade/preferences_test.c -o "$vmd_output/preferences"
"$vmd_output/preferences"
printf 'Language host test artifacts: %s\n' "$vmd_output"
