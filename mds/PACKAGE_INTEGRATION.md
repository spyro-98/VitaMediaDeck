# Standalone package integration

VitaMediaDeck consumes three independent repositories. The default layout keeps
them side by side:

```text
personale/
├── VitaMediaDeck/
├── vita-hw-decoder/
├── vita-sw-decoder/
└── vita-https/
```

The paths can instead be supplied during configuration:

```sh
cmake -S . -B build \
  -DVITAMEDIADECK_HW_DECODER_PACKAGE=/absolute/path/vita-hw-decoder \
  -DVITAMEDIADECK_SW_DECODER_PACKAGE=/absolute/path/vita-sw-decoder \
  -DVITAMEDIADECK_HTTPS_PACKAGE=/absolute/path/vita-https
```

## What VitaMediaDeck links

```cmake
add_subdirectory("${VITAMEDIADECK_HW_DECODER_PACKAGE}" ...)
add_subdirectory("${VITAMEDIADECK_SW_DECODER_PACKAGE}" ...)
add_subdirectory("${VITAMEDIADECK_HTTPS_PACKAGE}" ...)

target_link_libraries(vitamediadeck PRIVATE
  VitaHwDecoder::VitaHwDecoder
  VitaSwDecoder::VitaSwDecoder
  VitaHttps::VitaHttps)
```

The two decoder libraries intentionally expose parallel lifecycles but distinct
symbols. This allows both static libraries to coexist in one Vita ELF. Their
internal decode, audio and presentation symbols are also namespaced so the
static linker cannot accidentally satisfy one backend with the other.

## Runtime selection

`src/media/vita_decoder.c` owns selection, not either package:

1. create fresh audio/video cursors and open `vita-hw-decoder`;
2. if hardware open fails, destroy it and open `vita-sw-decoder`;
3. if `h264_vita` fails later while receiving the first access units, close
   the hardware session and reopen software near the last media timestamp;
4. expose one neutral status to the existing playback screen, including the
   active HW/SW badge.

This keeps fallback policy in the application while each decoder package stays
deterministic and reusable.

## HTTPS ownership

VitaMediaDeck calls `vita_https_init()` before network-source initialization and
`vita_https_shutdown()` at application teardown. Jellyfin authentication,
library browsing, and poster retrieval use `vita_https_perform()`. Jellyfin
direct play uses authenticated `HEAD` and bounded Range requests. Its access
token stays in the session credential structure and is never serialized.
WebDAV listing uses `vita_https_perform()`. WebDAV playback uses
`vita_https_open_range_stream()`, which verifies an actual one-byte `206`
response and returns a cached seekable cursor. VitaMediaDeck does not initialize
libcurl/Mbed TLS or embed a second CA bundle.

The pinned non-PIC Jansson archive parses bounded Jellyfin API responses; the
ordinary VitaSDK archive is not linked because its PIC relocations are rejected
by `vita-elf-create`. SFTP and SMB keep their protocol libraries but share the
network lifecycle initialized by `vita-https`.

For release builds, `vita-https/tools/build-curl-mbedtls.sh` must run first and
`VITAMEDIADECK_HTTPS_CURL_ROOT` must identify its output. The ordinary VitaSDK
libcurl/OpenSSL archives are not an accepted fallback. `tools/build-libssh2-vita.sh`
also builds libssh2 with Mbed TLS so the final executable contains no legacy
OpenSSL symbols.
