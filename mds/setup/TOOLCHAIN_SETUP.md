# VitaTube toolchain setup

## Host prerequisites

Install VitaSDK following the current instructions at https://vitasdk.org/ and
make the target compiler available:

```sh
export VITASDK=/absolute/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

The host also needs CMake, Git, Patch, Make/Ninja, curl, tar, and normal archive
utilities.

Verify the toolchain:

```sh
test -x "$VITASDK/bin/arm-vita-eabi-gcc"
test -f "$VITASDK/share/vita.toolchain.cmake"
test -f "$VITASDK/share/vita.cmake"
arm-vita-eabi-gcc --version
```

## Vita port libraries

Install Vita-targeted packages through `vdpm`; do not link host libraries from
Homebrew or the Linux distribution. The application requires the Vita ports of:

```text
libvita2d freetype libpng libjpeg-turbo zlib bzip2 mpg123
mbedtls libxml2 zstd libsmb2
```

The exact archive order in `CMakeLists.txt` is authoritative.

## Build pinned libssh2

```sh
./tools/build-libssh2-vita.sh
```

The script builds libssh2 1.11.1 statically with the Vita toolchain, Mbed TLS,
and zlib, then installs headers, archive, and license data below
`build/deps/libssh2-vita`.

## Build the pinned HTTPS stack

```sh
../vita-https/tools/build-curl-mbedtls.sh
```

This creates a dedicated libcurl 8.21.0 archive using Mbed TLS 3.6.5. The
legacy VitaSDK OpenSSL libcurl is intentionally not a release dependency.

## Build the pinned FFmpeg hardware backend

```sh
./tools/build-ffmpeg-vita-hw.sh
```

The script clones pinned FFmpeg and wiliwili revisions into a temporary
directory, verifies the Vita patch checksum, enables the required H.264/AAC and
MOV/MP4 pieces, builds with Cortex-A9/NEON `-O3` and LTO, verifies the
`h264_vita` registration symbol, and installs below
`build/deps/ffmpeg-vita-hw`.

Optional custom prefixes:

```sh
export VITATUBE_H264_VITA_ROOT=/absolute/ffmpeg-prefix
./tools/build-ffmpeg-vita-hw.sh

cmake -S . -B build \
  -DVITATUBE_H264_VITA_ROOT="$VITATUBE_H264_VITA_ROOT" \
  -DVITATUBE_LIBSSH2_ROOT=/absolute/libssh2-prefix \
  -DVITATUBE_HTTPS_CURL_ROOT=/absolute/curl-mbedtls-prefix
```

## Build VitaTube

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
unzip -t build/VitaTube.vpk
shasum -a 256 build/VitaTube.vpk
```

Expected outputs include `build/vitatube`, `build/vitatube.self`, and
`build/VitaTube.vpk`.

## Packaging profile

The hardware-decoder configuration creates a normal unsafe homebrew SELF with
ASLR disabled, as required by the working public backend. It does not request a
custom shell authority or a physical-contiguous-memory budget. The VPK includes
the application assets, font, runtime module, and applicable license texts.

## Troubleshooting

### Missing dependency archive

Check that the file exists below `$VITASDK/arm-vita-eabi/lib` and is an ARM Vita
archive. Run the corresponding `vdpm` package or pinned builder again.

### Missing `libssh2.a`

Run `tools/build-libssh2-vita.sh`, or pass the matching prefix with
`-DVITATUBE_LIBSSH2_ROOT=...`.

### Missing `h264_vita`

Run `tools/build-ffmpeg-vita-hw.sh`. Do not substitute a generic FFmpeg archive
that lacks the patched decoder.

### Static link errors

Archive ordering matters. Inspect undefined symbols with `arm-vita-eabi-nm`
and preserve the order in the root CMake file.

### A successful build fails on device

Treat CMake, `nm`, and `unzip -t` as local validation only. Network TLS, server
authentication, shell background-audio policy, decoder surfaces, and teardown
must be tested on physical hardware.

## Sources

- https://vitasdk.org/
- https://github.com/vitasdk/vita-toolchain
- https://github.com/vitasdk/packages
- https://cmake.org/documentation/
