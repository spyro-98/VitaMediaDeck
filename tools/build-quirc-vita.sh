#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
: "${VITASDK:?Set VITASDK to the VitaSDK installation directory}"
VITA_SDK_ROOT=$VITASDK
QUIRC_COMMIT=542848dd6b9b0eaa9587bbf25b9bc67bd8a71fca
SOURCE_ROOT=${VITAMEDIADECK_QUIRC_SOURCE:-$PROJECT_ROOT/build/deps/src/quirc-1.2}
PREFIX=${VITAMEDIADECK_QUIRC_ROOT:-$PROJECT_ROOT/build/deps/quirc-vita}
OBJECT_ROOT=$PROJECT_ROOT/build/deps/quirc-vita-objects

if [ ! -d "$SOURCE_ROOT/.git" ]; then
  mkdir -p "$(dirname -- "$SOURCE_ROOT")"
  git clone https://github.com/dlbeer/quirc.git "$SOURCE_ROOT"
fi

ACTUAL_COMMIT=$(git -C "$SOURCE_ROOT" rev-parse HEAD)
if [ "$ACTUAL_COMMIT" != "$QUIRC_COMMIT" ]; then
  git -C "$SOURCE_ROOT" fetch --depth 1 origin "$QUIRC_COMMIT"
  git -C "$SOURCE_ROOT" checkout --detach "$QUIRC_COMMIT"
fi

mkdir -p "$OBJECT_ROOT" "$PREFIX/lib" "$PREFIX/include"
CC=$VITA_SDK_ROOT/bin/arm-vita-eabi-gcc
AR=$VITA_SDK_ROOT/bin/arm-vita-eabi-ar
CFLAGS="-O3 -std=gnu11 -Wall -ffunction-sections -fdata-sections -DQUIRC_MAX_REGIONS=4096 -DQUIRC_FLOAT_TYPE=float -DQUIRC_USE_TGMATH -I$SOURCE_ROOT/lib"

for SOURCE in decode identify quirc version_db; do
  "$CC" $CFLAGS -c "$SOURCE_ROOT/lib/$SOURCE.c" -o "$OBJECT_ROOT/$SOURCE.o"
done

"$AR" rcs "$PREFIX/lib/libquirc.a" \
  "$OBJECT_ROOT/decode.o" \
  "$OBJECT_ROOT/identify.o" \
  "$OBJECT_ROOT/quirc.o" \
  "$OBJECT_ROOT/version_db.o"
cp "$SOURCE_ROOT/lib/quirc.h" "$PREFIX/include/quirc.h"

printf '%s\n' "Built $PREFIX/lib/libquirc.a"
printf '%s\n' "quirc commit: $QUIRC_COMMIT"
printf '%s\n' "flags: -O3 QUIRC_MAX_REGIONS=4096 QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH"
