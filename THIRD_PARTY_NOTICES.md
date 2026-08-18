# Third-party notices

VitaTube is distributed under GPL-3.0-only. See `LICENSE`. The following
components remain governed by their own licenses and copyright notices.

## FFmpeg and h264_vita

The reproducible hardware backend is built from FFmpeg commit
`ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2` (the `n6.0` release) and the Vita
patch from wiliwili commit `88e5876bea9502d06f46a8656e3530684d3aaf7d`.
The selected FFmpeg configuration is LGPL-2.1-or-later; wiliwili is
GPL-3.0. `tools/build-ffmpeg-vita-hw.sh` records and verifies the exact inputs
and installs the FFmpeg LGPL text for inclusion in the VPK.

- https://github.com/FFmpeg/FFmpeg
- https://github.com/xfangfang/wiliwili

## Network libraries

- **libcurl 8.21.0**, curl license (MIT/X-derived), built only with Mbed TLS:
  https://curl.se/libcurl/
- **libssh2**, BSD 3-Clause: https://github.com/libssh2/libssh2
- **libsmb2**, LGPL-2.1-or-later: https://github.com/sahlberg/libsmb2
- **libxml2**, MIT: https://gitlab.gnome.org/GNOME/libxml2
- **Mbed TLS 3.6.5**, Apache-2.0 OR GPL-2.0-or-later; VitaTube selects
  Apache-2.0: https://github.com/Mbed-TLS/mbedtls
- **zstd**, BSD 3-Clause or GPL-2.0: https://github.com/facebook/zstd
- **zlib**, zlib license: https://zlib.net/

libcurl and libssh2 are both built with the Mbed TLS backend. OpenSSL is not
part of a releasable VitaTube binary. The pinned builders install licenses,
source archives and build provenance beside their generated libraries.

## Media, UI and runtime libraries

- **ReAvPlayer**, MIT. The notice is preserved in
  `licenses/ReAvPlayer-MIT.txt` and packaged beside the module.
- **mpg123**, LGPL-2.1-or-later: https://www.mpg123.de/
- **vita2d**, MIT: https://github.com/xerpi/libvita2d
- **FreeType**, FreeType License or GPL-2.0: https://freetype.org/license.html
- **libjpeg-turbo**, BSD-style/IJG/zlib licenses:
  https://github.com/libjpeg-turbo/libjpeg-turbo
- **libpng**, libpng license: http://www.libpng.org/pub/png/libpng.html
- **bzip2**, bzip2 license: https://sourceware.org/bzip2/
- **PThread-Embedded**, its upstream open-source terms:
  https://github.com/vitasdk/pthread-embedded

This software is based in part on the work of the Independent JPEG Group.

ReAvPlayer upstream: https://github.com/SonicMastr/ReAvPlayer

## Font

Inter 4.1 Medium and SemiBold are bundled under the SIL Open Font License 1.1.
Its text is stored in `assets/fonts/Inter-OFL.txt` and packaged as
`licenses/Inter-OFL.txt`.

## VitaSDK

VitaTube is built with VitaSDK, vita-toolchain, Vita headers, and VitaSDK port
packages. Each project and package keeps its own license:

- https://vitasdk.org/
- https://github.com/vitasdk
- https://github.com/vitasdk/packages

## Trademarks

Component license texts included in the VPK remain available in its
`licenses/` directory. PlayStation, PS Vita, Sony, and related marks belong to
their respective owners. VitaTube is an independent homebrew project.

The repository and VPK do not include Sony console photographs, system
software, keys or proprietary SDK files. The application name and original
artwork do not imply endorsement by Sony or any media platform.
