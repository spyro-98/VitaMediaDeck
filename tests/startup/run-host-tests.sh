#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_out="$(mktemp -d "${TMPDIR:-/tmp}/vmd-startup.XXXXXX")"
clang -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
 -Isrc -Itests/home_tabs/include tests/startup/runtime_test.c -o "$vmd_out/runtime"
"$vmd_out/runtime"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Isrc tests/startup/wave_test.c -lm -o "$vmd_out/waves"
"$vmd_out/waves"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Isrc tests/startup/crc_test.c -o "$vmd_out/crc"
"$vmd_out/crc"
clang -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -fsanitize=address,undefined -Isrc -I../vita-https/include \
 -Itests/podcast_resume/include -Itests/podcast_metadata/include \
 tests/startup/library_replay_test.c -lpthread -o "$vmd_out/library-replay"
"$vmd_out/library-replay"
