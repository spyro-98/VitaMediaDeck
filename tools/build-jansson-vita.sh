#!/usr/bin/env bash
set -euo pipefail

version=2.15.1
source_sha256=dbf95cb0af903f4fb8b61507d96b45b67db7d1479688ede352e1d571394d06f7
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
prefix="${VITAMEDIADECK_JANSSON_ROOT:-$repo_root/build/deps/jansson-vita}"
work="$(mktemp -d "${TMPDIR:-/tmp}/vitamediadeck-jansson.XXXXXX")"
trap 'rm -rf "$work"' EXIT

: "${VITASDK:?Set VITASDK to the VitaSDK root}"
archive="$work/jansson-$version.tar.gz"
curl -L --fail --retry 3 --max-time 300 \
  "https://github.com/akheron/jansson/archive/refs/tags/v$version.tar.gz" \
  -o "$archive"
printf '%s  %s\n' "$source_sha256" "$archive" | shasum -a 256 -c -
tar -xzf "$archive" -C "$work"
patch -d "$work/jansson-$version" -p1 \
  < "$repo_root/tools/patches/jansson-vita-no-pic.patch"

cmake -S "$work/jansson-$version" -B "$work/build" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DJANSSON_BUILD_SHARED_LIBS=OFF \
  -DJANSSON_BUILD_DOCS=OFF \
  -DJANSSON_WITHOUT_TESTS=ON \
  -DJANSSON_EXAMPLES=OFF \
  -DUSE_URANDOM=OFF
cmake --build "$work/build" --parallel
cmake --install "$work/build"
install -m 0644 "$work/jansson-$version/LICENSE" "$prefix/LICENSE"
install -m 0644 "$archive" "$prefix/jansson-$version.tar.gz"

if "$VITASDK/bin/arm-vita-eabi-readelf" -r "$prefix/lib/libjansson.a" |
   grep -Eq 'R_ARM_BASE_PREL|R_ARM_GOT_BREL'; then
  echo "Jansson archive still contains Vita-incompatible PIC relocations" >&2
  exit 1
fi

echo "Jansson $version Vita archive is ready at $prefix"
