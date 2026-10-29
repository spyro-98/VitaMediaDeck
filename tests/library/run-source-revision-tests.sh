#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_out=$(mktemp -d /private/tmp/vmd-source-revision.XXXXXX)
python3 - "$vmd_out/source-save.inc" <<'PY'
from pathlib import Path
import sys
s=Path('src/network/network_source.c').read_text()
Path(sys.argv[1]).write_text(s[:s.index('static void escape_password(')])
PY
for vmd_sanitizer in address,undefined thread;do
 clang -O1 -g -Wall -Wextra -Werror -fsanitize="$vmd_sanitizer" -ffunction-sections -fdata-sections \
  -Isrc -I../vita-https/include -Itests/podcast_metadata/include -I"$vmd_out" $(pkg-config --cflags libssh2) \
  tests/library/source_revision_test.c -Wl,-dead_strip -o "$vmd_out/test"
 "$vmd_out/test"
done
