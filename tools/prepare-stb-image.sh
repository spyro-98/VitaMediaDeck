#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
STB_COMMIT=2c980bb59875b0d32144a71867fbdebb2f77cd20
SOURCE_ROOT=${VITAMEDIADECK_STB_SOURCE:-$PROJECT_ROOT/build/deps/src/stb}
PREFIX=${VITAMEDIADECK_STB_ROOT:-$PROJECT_ROOT/build/deps/stb}

if [ ! -d "$SOURCE_ROOT/.git" ]; then
  mkdir -p "$(dirname -- "$SOURCE_ROOT")"
  git clone https://github.com/nothings/stb.git "$SOURCE_ROOT"
fi

ACTUAL_COMMIT=$(git -C "$SOURCE_ROOT" rev-parse HEAD)
if [ "$ACTUAL_COMMIT" != "$STB_COMMIT" ]; then
  git -C "$SOURCE_ROOT" fetch --depth 1 origin "$STB_COMMIT"
  git -C "$SOURCE_ROOT" checkout --detach "$STB_COMMIT"
fi

mkdir -p "$PREFIX/include"
cp "$SOURCE_ROOT/stb_image.h" "$PREFIX/include/stb_image.h"
cp "$SOURCE_ROOT/LICENSE" "$PREFIX/LICENSE"

printf '%s\n' "Prepared $PREFIX/include/stb_image.h"
printf '%s\n' "stb commit: $STB_COMMIT"
