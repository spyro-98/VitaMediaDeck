#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$vmd_repo"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-library.XXXXXX")"
python3 tests/library/extract-background.py "$vmd_output/background-sample.inc"
python3 tests/library/extract-local.py "$vmd_output/local-search.inc"
for sanitizer in address,undefined thread; do
 clang -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize="$sanitizer" -Isrc -I../vita-https/include -Itests/podcast_resume/include -Itests/podcast_metadata/include tests/library/core_test.c -lpthread -o "$vmd_output/core-test"
 clang -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize="$sanitizer" -ffunction-sections -fdata-sections -DSCE_O_APPEND=O_APPEND -Isrc -I../vita-https/include -Itests/podcast_resume/include -Itests/podcast_metadata/include tests/library/search_test.c src/library/library.c src/library/catalog.c -lpthread -Wl,-dead_strip -o "$vmd_output/search-test"
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$sanitizer" -Isrc -I"$vmd_output" tests/library/background_test.c -lpthread -o "$vmd_output/background-test"
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$sanitizer" -ffunction-sections -fdata-sections -DSCE_O_APPEND=O_APPEND -Isrc -I../vita-https/include -Itests/podcast_metadata/include -Itests/podcast_resume/include -I"$vmd_output" tests/library/local_search_test.c src/library/catalog.c src/library/library.c -lpthread -Wl,-dead_strip -o "$vmd_output/local-search-test"
 "$vmd_output/local-search-test"
 "$vmd_output/background-test"
 "$vmd_output/search-test"
 "$vmd_output/core-test"
done
printf 'Unified library host evidence: %s\n' "$vmd_output"
