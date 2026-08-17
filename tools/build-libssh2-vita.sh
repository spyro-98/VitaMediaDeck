#!/usr/bin/env bash
set -euo pipefail

LIBSSH2_VERSION=1.11.1
LIBSSH2_SHA256=d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE_ROOT="$PROJECT_ROOT/build/deps/src"
ARCHIVE="$SOURCE_ROOT/libssh2-$LIBSSH2_VERSION.tar.gz"
SOURCE_DIR="$SOURCE_ROOT/libssh2-$LIBSSH2_VERSION"
BUILD_DIR="$PROJECT_ROOT/build/deps/libssh2-build"
PREFIX="$PROJECT_ROOT/build/deps/libssh2-vita"

: "${VITASDK:?Set VITASDK to the VitaSDK installation directory}"
mkdir -p "$SOURCE_ROOT"

if [[ ! -f "$ARCHIVE" ]]; then
  curl -L --fail --retry 3 \
    "https://github.com/libssh2/libssh2/releases/download/libssh2-$LIBSSH2_VERSION/libssh2-$LIBSSH2_VERSION.tar.gz" \
    -o "$ARCHIVE"
fi
printf '%s  %s\n' "$LIBSSH2_SHA256" "$ARCHIVE" | shasum -a 256 -c -
if [[ ! -d "$SOURCE_DIR" ]]; then
  tar -xzf "$ARCHIVE" -C "$SOURCE_ROOT"
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_STATIC_LIBS=ON \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_TESTING=OFF \
  -DCRYPTO_BACKEND=mbedTLS \
  -DENABLE_ZLIB_COMPRESSION=ON \
  -DENABLE_DEBUG_LOGGING=OFF \
  -DPICKY_COMPILER=OFF
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR"

mkdir -p "$PREFIX/share/sources" "$PREFIX/share/licenses"
install -m 0644 "$ARCHIVE" "$PREFIX/share/sources/libssh2-$LIBSSH2_VERSION.tar.gz"
install -m 0644 "$SOURCE_DIR/COPYING" "$PREFIX/share/licenses/libssh2-BSD-3-Clause.txt"

NM_OUTPUT="$BUILD_DIR/libssh2.undefined.txt"
"$VITASDK/bin/arm-vita-eabi-gcc-nm" -u "$PREFIX/lib/libssh2.a" > "$NM_OUTPUT"
if grep -Eq 'OPENSSL_|SSL_(CTX|connect|read|write)' "$NM_OUTPUT"; then
  echo "Refusing libssh2: OpenSSL symbols were detected" >&2
  exit 1
fi
if ! grep -q 'mbedtls_' "$NM_OUTPUT"; then
  echo "Refusing libssh2: Mbed TLS symbols were not detected" >&2
  exit 1
fi

cat > "$PREFIX/share/BUILD-PROVENANCE.txt" <<EOF
libssh2.version=$LIBSSH2_VERSION
libssh2.source.sha256=$LIBSSH2_SHA256
libssh2.crypto_backend=Mbed TLS
EOF

printf 'libssh2 %s installed in %s\n' "$LIBSSH2_VERSION" "$PREFIX"
