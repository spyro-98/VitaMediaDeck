## VitaMediaDeck CMake project.
##
## Structure verified against the official VitaSDK samples:
## https://github.com/vitasdk/samples/blob/master/hello_world/CMakeLists.txt
## https://github.com/vitasdk/samples/blob/master/ctrl/CMakeLists.txt
##
## and against the CMake macros defined by vita-toolchain's vita.cmake:
## https://github.com/vitasdk/vita-toolchain/blob/master/cmake_toolchain/vita.cmake

cmake_minimum_required(VERSION 3.16)

## This must precede project(): it selects VitaSDK's toolchain file while
## still allowing callers to pass CMAKE_TOOLCHAIN_FILE explicitly.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "VITASDK is not defined. Install VitaSDK and point VITASDK at its absolute directory; see mds/setup/TOOLCHAIN_SETUP.md.")
  endif()
endif()

project(vitamediadeck C)

## Complete public hardware-decoder backend from wiliwili's FFmpeg-vita
## patch.  This is deliberately a whole FFmpeg codec implementation, not the
## old hand-written sequence of individual SceAvcdec calls.  The reproducible
## builder pins both upstream revisions and installs headers/libraries below
## build/deps, keeping generated third-party objects out of git.
option(VITAMEDIADECK_ENABLE_H264_VITA
  "Use the complete FFmpeg h264_vita hardware decoder" ON)
if(NOT VITAMEDIADECK_ENABLE_H264_VITA)
  message(FATAL_ERROR
    "The packaged VitaMediaDeck playback stack requires vita-hw-decoder; "
    "software fallback is selected at runtime, not as a build replacement.")
endif()
set(VITAMEDIADECK_H264_VITA_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/ffmpeg-vita-hw"
  CACHE PATH "Prefix produced by tools/build-ffmpeg-vita-hw.sh")

## Standalone packages live in independent sibling repositories by default.
## Override these cache paths when using submodules or an installed layout.
set(VITAMEDIADECK_HW_DECODER_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/../vita-hw-decoder"
  CACHE PATH "Path to the vita-hw-decoder repository")
set(VITAMEDIADECK_SW_DECODER_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/../vita-sw-decoder"
  CACHE PATH "Path to the vita-sw-decoder repository")
set(VITAMEDIADECK_HTTPS_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/../vita-https"
  CACHE PATH "Path to the vita-https repository")
set(VITAMEDIADECK_HTTPS_CURL_ROOT
  "${VITAMEDIADECK_HTTPS_PACKAGE}/build/deps/curl-mbedtls"
  CACHE PATH "Pinned curl/Mbed TLS prefix produced by vita-https")
foreach(package_root IN ITEMS
    "${VITAMEDIADECK_HW_DECODER_PACKAGE}"
    "${VITAMEDIADECK_SW_DECODER_PACKAGE}"
    "${VITAMEDIADECK_HTTPS_PACKAGE}")
  if(NOT EXISTS "${package_root}/CMakeLists.txt")
    message(FATAL_ERROR "Missing VitaMediaDeck package repository: ${package_root}")
  endif()
endforeach()

set(VITAMEDIADECK_LIBSSH2_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/libssh2-vita"
  CACHE PATH "Prefix produced by tools/build-libssh2-vita.sh")
set(VITAMEDIADECK_LIBSSH2_ARCHIVE "${VITAMEDIADECK_LIBSSH2_ROOT}/lib/libssh2.a")
if(NOT EXISTS "${VITAMEDIADECK_LIBSSH2_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITAMEDIADECK_LIBSSH2_ARCHIVE}; run tools/build-libssh2-vita.sh before building VitaMediaDeck.")
endif()
add_library(vitamediadeck_libssh2 STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_libssh2 PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_LIBSSH2_ARCHIVE}"
  INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto;z")

# VitaSDK's packaged Jansson archive is built as PIC and contains
# R_ARM_BASE_PREL relocations that vita-elf-create cannot encode. Use the
# pinned non-PIC archive produced specifically for the Vita executable.
set(VITAMEDIADECK_JANSSON_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/jansson-vita"
  CACHE PATH "Prefix produced by tools/build-jansson-vita.sh")
set(VITAMEDIADECK_JANSSON_ARCHIVE
  "${VITAMEDIADECK_JANSSON_ROOT}/lib/libjansson.a")
if(NOT EXISTS "${VITAMEDIADECK_JANSSON_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITAMEDIADECK_JANSSON_ARCHIVE}; run tools/build-jansson-vita.sh before building VitaMediaDeck.")
endif()
add_library(vitamediadeck_jansson STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_jansson PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_JANSSON_ARCHIVE}"
  INTERFACE_INCLUDE_DIRECTORIES "${VITAMEDIADECK_JANSSON_ROOT}/include")

set(VITAMEDIADECK_QUIRC_ARCHIVE
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/quirc-vita/lib/libquirc.a"
  CACHE FILEPATH "quirc archive built by tools/build-quirc-vita.sh")
if(NOT EXISTS "${VITAMEDIADECK_QUIRC_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITAMEDIADECK_QUIRC_ARCHIVE}; run tools/build-quirc-vita.sh. "
    "The generic VitaSDK archive has a 254-region limit that is insufficient "
    "for the 480x480 camera scanner.")
endif()
add_library(vitamediadeck_quirc STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_quirc PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_QUIRC_ARCHIVE}")

set(VITAMEDIADECK_STB_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/stb"
  CACHE PATH "Pinned stb_image prefix produced by tools/prepare-stb-image.sh")
if(NOT EXISTS "${VITAMEDIADECK_STB_ROOT}/include/stb_image.h" OR
   NOT EXISTS "${VITAMEDIADECK_STB_ROOT}/LICENSE")
  message(FATAL_ERROR
    "Missing pinned stb_image files; run tools/prepare-stb-image.sh before building VitaMediaDeck.")
endif()

set(VITAMEDIADECK_RELEASE_LICENSE_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/release-licenses"
  CACHE PATH "License bundle produced by tools/prepare-release-licenses.sh")
set(VITAMEDIADECK_REQUIRED_RELEASE_LICENSES
  libsmb2-NOTICE.txt libsmb2-LGPL-2.1.txt libxml2-MIT.txt
  mpg123-COPYING.txt FLAC-COPYING.Xiph.txt libogg-COPYING.txt
  vita2d-MIT.txt FreeType-FTL.txt
  libjpeg-turbo-LICENSE.md libjpeg-turbo-README.ijg libpng-LICENSE.txt
  bzip2-LICENSE.txt jansson-MIT.txt pthread-embedded-COPYING.txt
  pthread-embedded-LGPL-2.1.txt pthread-embedded-Vita-MIT.txt)
foreach(license_file IN LISTS VITAMEDIADECK_REQUIRED_RELEASE_LICENSES)
  if(NOT EXISTS "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/${license_file}")
    message(FATAL_ERROR
      "Missing release license ${license_file}; run "
      "tools/prepare-release-licenses.sh before configuring VitaMediaDeck.")
  endif()
endforeach()

## The pinned builder enables MOV/MP4, Matroska and standalone SRT demuxers plus
## the complete hardware decoder backend. Provenance is documented by the build
## script.
set(VITAMEDIADECK_FFMPEG_ROOT "${VITAMEDIADECK_H264_VITA_ROOT}")

set(VITAMEDIADECK_AVFORMAT_ARCHIVE "${VITAMEDIADECK_FFMPEG_ROOT}/lib/libavformat.a")
if(NOT EXISTS "${VITAMEDIADECK_AVFORMAT_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITAMEDIADECK_AVFORMAT_ARCHIVE}; run tools/build-ffmpeg-vita-hw.sh before building VitaMediaDeck.")
endif()
add_library(vitamediadeck_avformat STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_avformat PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_AVFORMAT_ARCHIVE}")

set(VITAMEDIADECK_AVCODEC_ARCHIVE "${VITAMEDIADECK_FFMPEG_ROOT}/lib/libavcodec.a")
if(NOT EXISTS "${VITAMEDIADECK_AVCODEC_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITAMEDIADECK_AVCODEC_ARCHIVE}; run tools/build-ffmpeg-vita-hw.sh before building VitaMediaDeck.")
endif()
add_library(vitamediadeck_avcodec STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_avcodec PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_AVCODEC_ARCHIVE}")

set(VITAMEDIADECK_AVUTIL_ARCHIVE "${VITAMEDIADECK_FFMPEG_ROOT}/lib/libavutil.a")
if(NOT EXISTS "${VITAMEDIADECK_AVUTIL_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITAMEDIADECK_AVUTIL_ARCHIVE}; run tools/build-ffmpeg-vita-hw.sh before building VitaMediaDeck.")
endif()
add_library(vitamediadeck_avutil STATIC IMPORTED GLOBAL)
set_target_properties(vitamediadeck_avutil PROPERTIES
  IMPORTED_LOCATION "${VITAMEDIADECK_AVUTIL_ARCHIVE}")
set(VITAMEDIADECK_AVUTIL_LINK vitamediadeck_avutil)
set(VITAMEDIADECK_FFMPEG_LICENSE
    "${VITAMEDIADECK_FFMPEG_ROOT}/share/licenses/FFmpeg-LGPL-2.1.txt")
if(NOT EXISTS "${VITAMEDIADECK_FFMPEG_LICENSE}")
  message(FATAL_ERROR
    "Missing ${VITAMEDIADECK_FFMPEG_LICENSE}; rerun tools/build-ffmpeg-vita-hw.sh.")
endif()
set(VITAMEDIADECK_FFMPEG_LICENSE_RESOURCE
    FILE "${VITAMEDIADECK_FFMPEG_LICENSE}" licenses/FFmpeg-LGPL-2.1.txt)
set(VITAMEDIADECK_WILIWILI_LICENSE
    "${VITAMEDIADECK_FFMPEG_ROOT}/share/licenses/wiliwili-GPL-3.0.txt")
if(NOT EXISTS "${VITAMEDIADECK_WILIWILI_LICENSE}")
  message(FATAL_ERROR
    "Missing ${VITAMEDIADECK_WILIWILI_LICENSE}; rerun tools/build-ffmpeg-vita-hw.sh.")
endif()

## Without an explicit default, documented `cmake -S/-B` commands produced an
## unoptimized binary. On a 444 MHz Cortex-A9, unoptimized video, UI, audio,
## and media parsing work looks like a functional decoder failure. Use Release by
## default. The unstripped intermediate ELF remains available for
## vita-parse-core independently of this optimization choice.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

option(VITAMEDIADECK_ENABLE_LTO
  "Enable link-time optimization for production Vita builds" ON)

## Load VitaSDK's packaging macros after project(). The verified module name is
## vita.cmake, not VitaSDK.cmake.
include("${VITASDK}/share/vita.cmake" REQUIRED)

## --- Application metadata ------------------------------------------------

# Name displayed below the LiveArea bubble.
set(VITA_APP_NAME "VitaMediaDeck")

# TITLEID must contain exactly nine characters.
set(VITA_TITLEID  "VMDK00001")

# LiveArea version; vita_create_vpk requires the ##.## format.
set(VITA_VERSION  "01.02")

## --- Compiler and linker flags --------------------------------------------

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=gnu11")
add_link_options("LINKER:-z,noexecstack")

## --- Include paths --------------------------------------------------------

include_directories(
  $<$<BOOL:${VITAMEDIADECK_ENABLE_H264_VITA}>:${VITAMEDIADECK_H264_VITA_ROOT}/include>
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VITAMEDIADECK_LIBSSH2_ROOT}/include
  ${VITAMEDIADECK_STB_ROOT}/include
  ${VITASDK}/arm-vita-eabi/include/libxml2
)

## --- Sources --------------------------------------------------------------

set(VITA_HW_DECODER_FFMPEG_ROOT "${VITAMEDIADECK_H264_VITA_ROOT}"
    CACHE PATH "FFmpeg prefix consumed by vita-hw-decoder" FORCE)
set(VITA_SW_DECODER_FFMPEG_ROOT "${VITAMEDIADECK_H264_VITA_ROOT}"
    CACHE PATH "FFmpeg prefix consumed by vita-sw-decoder" FORCE)
set(VITA_HTTPS_CURL_ROOT "${VITAMEDIADECK_HTTPS_CURL_ROOT}"
    CACHE PATH "curl/Mbed TLS prefix consumed by vita-https" FORCE)
add_subdirectory("${VITAMEDIADECK_HW_DECODER_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-hw-decoder")
add_subdirectory("${VITAMEDIADECK_SW_DECODER_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-sw-decoder")
add_subdirectory("${VITAMEDIADECK_HTTPS_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-https")

# vita-https carries a conservative private -O2 default. Keep VitaMediaDeck's
# explicit release contract authoritative by appending -O3 after the package
# has declared its own options; GCC applies the last optimization level.
target_compile_options(vita_https PRIVATE $<$<CONFIG:Release>:-O3>)

add_executable(${PROJECT_NAME}
  src/main.c
  src/common/debugScreen.c
  src/common/text_log.c
  src/i18n/i18n.c
  src/i18n/strings.c
  src/network/network_source.c
  src/network/webdav_source.c
  src/network/sftp_source.c
  src/network/smb_source.c
  src/network/jellyfin_source.c
  src/network/download_manager.c
  src/media/background_playback.c
  src/media/audio_volume.c
  src/media/avplayer_init.c
  src/media/media_tracks.c
  src/media/vita_decoder.c
  src/media/video_thumbnail.c
  src/media/hw_player_screen.c
  src/media/image_loader.c
  src/media/music_metadata.c
  src/history/playback_history.c
  src/settings/preferences.c
  src/system/background_audio.c
  src/system/display_awake.c
  src/system/performance.c
  src/ui/font.c
  src/ui/format.c
  src/ui/components.c
  src/ui/runtime.c
  src/ui/focus_glow.c
  src/ui/brand.c
  src/ui/touch.c
  src/ui/text_input.c
  src/ui/loading_screen.c
  src/ui/image_viewer.c
  src/ui/mini_player.c
  src/ui/sections_sidebar.c
  src/ui/local_media_screen.c
  src/ui/local_files_screen.c
  src/ui/music_player.c
  src/ui/settings_screen.c
  src/ui/about_screen.c
  src/ui/log_viewer.c
  src/ui/text_reader.c
  src/ui/network_sources_screen.c
  src/ui/qr_scanner.c
)

# The software H.264 path is CPU-bound on the three user cores of the Vita.
# Keep the final application and the vendored FFmpeg LTO objects in one link-
# time optimization unit. GCC treats -O4 exactly like -O3; -O3 is the highest
# distinct standard optimization level and is already selected by Release.
target_compile_options(${PROJECT_NAME} PRIVATE
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -ffunction-sections -fdata-sections)
target_link_options(${PROJECT_NAME} PRIVATE
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -Wl,--gc-sections)
if(VITAMEDIADECK_ENABLE_LTO)
  target_compile_options(${PROJECT_NAME} PRIVATE -flto=4 -fuse-linker-plugin)
  target_link_options(${PROJECT_NAME} PRIVATE -flto=4 -fuse-linker-plugin)
endif()

## SceLibKernel_stub, SceKernelThreadMgr_stub, and SceProcessmgr_stub are among
## the common stubs linked by the toolchain and are not listed explicitly.
##
## Standalone package targets carry their public static dependencies. Their
## position before application-specific protocol libraries preserves archive
## resolution order on the Vita linker.
target_link_libraries(${PROJECT_NAME}
  SceDisplay_stub     # psvDebugScreenPrintf() in src/common/debugScreen.c
  SceCtrl_stub        # controller sampling
  SceNet_stub         # sceNet and the BSD-compatible socket layer
  SceNetCtl_stub      # guarded by the vita-https lifecycle
  ## AvPlayer is supplied by reAvPlayer.suprx, but SceSysmodule remains needed
  ## for network, IME, codec, and notification modules.
  SceSysmodule_stub
  ## Read system language directly without the AppUtil/CommonDialog startup
  ## sequence that caused two hardware regressions.
  SceRegistryMgr_stub
  ## ScePower_stub: scePowerGetBatteryLifePercent() / scePowerIsBatteryCharging(),
  ## battery indicator in the topbar (src/ui/brand.c). Sampled from a 2s cache,
  ## not once per frame.
  ScePower_stub
  ## SceRtc_stub: sceRtcGetCurrentClockLocalTime(), clock indicator in the
  ## topbar (src/ui/brand.c). The 12h/24h choice reuses
  ## sceRegMgrSystemParamGetInt() above, so no extra stub for that.
  SceRtc_stub
  ## --- Decode and render chain ---------------------------------------------
  ## vita2d precedes its dependencies because it is a static archive. The list
  ## is reduced from the official sample to symbols used by VitaMediaDeck. JPEG and
  ## PNG thumbnails are decoded by their software libraries, not SceJpeg.
  vita2d
  ScePgf_stub         # sceFont* weak imports used by libvita2d system PGF CJK fallback
  jpeg                # vita2d image decoder dependency
  ## libpng depends on zlib; both are required by the PNG header/logo path.
  freetype            # Inter TTF rasterization at exact native UI sizes
  bz2                 # FreeType static dependency
  png
  z
  webpdecoder         # scaled, bounded WebP image decoding
  VitaHwDecoder::VitaHwDecoder
  VitaSwDecoder::VitaSwDecoder
  VitaHttps::VitaHttps
  vitamediadeck_avformat   # pinned FFmpeg with registered MOV/MP4 + Matroska demuxers
  vitamediadeck_avcodec    # complete h264_vita plus software H.264 fallback
  ${VITAMEDIADECK_AVUTIL_LINK}
  mpg123              # local MP3 playback
  FLAC                 # native local FLAC decoding, seeking, tags and pictures
  ogg                  # static libFLAC dependency supplied by the VitaSDK port
  vitamediadeck_libssh2    # authenticated SFTP with pinned host fingerprints
  smb2                # authenticated SMB2/SMB3 shares
  xml2                # namespace-safe WebDAV PROPFIND parser
  vitamediadeck_jansson # bounded Jellyfin API parsing; pinned non-PIC Vita archive
  SceAvPlayer_stub    # sceAvPlayerInit/AddSource/GetVideoData/... (src/media/)
  ## Public H.264/AAC codecs used by the reusable player. Private *Internal
  ## imports are intentionally excluded.
  SceVideodec_stub    # sceVideodecInitLibrary/TermLibrary + sceAvcdec* (same header/stub)
  SceCodecEngine_stub # SceVideodec_stub dependency verified by FFmpeg-vita configure
  SceAudiodec_stub    # sceAudiodecInitLibrary/CreateDecoder/Decode (AAC hardware)
  SceGxm_stub         # sceGxm* used by vita2d and sceGxmTextureInitLinear
  SceAppMgr_stub      # sceAppMgrGetBudgetInfo + _sceSharedFbOpen, required by vita2d.o
  SceCommonDialog_stub # sceCommonDialogUpdate, required internally by vita2d.o
  SceIme_stub         # sceImeOpen/Update/Close (src/ui/text_input.c)
  SceTouch_stub       # front panel: grids, selectors and player
  SceCamera_stub      # rear-camera QR link acquisition
  SceAudio_stub       # sceAudioOut* used by the audio-drain thread
  SceDisplay_stub     # repeated for vita2d's later one-pass references
  m                   # sinf/cosf used by vita2d
  pthread             # FFmpeg and libcurl worker support
  vitamediadeck_quirc # QR recognition for camera-acquired resource URLs
)

## --- Vita packaging -------------------------------------------------------

## Do not restore the former SceShell auth ID or custom PHYCONT/system budget.
##
## An earlier build created the SELF manually with vita-make-fself
## -m 0x12800 -pm 0x8000 -at 0x0E -a 0x2800000000000001 (system-authority
## privileges), inherited from a player that did not use reAvPlayer.
## After reAvPlayer integration, that combination stopped before main(),
## consistent with rejection of its 32 MB physically contiguous request.
##
## A byte-level comparison with a working Vita-Media-Player SELF confirmed a
## normal homebrew profile: no custom authority and no contiguous-memory
## budget. reAvPlayer fixes AvPlayer at the module boundary, so elevated
## authority was both unnecessary and harmful.
## wiliwili's production Vita build uses this exact public application
## profile for h264_vita: an unsafe homebrew SELF, no ASLR, and the maximum
## heap mode.  It does not request the custom SceShell auth ID or a PHYCONT
## budget that made an earlier VitaMediaDeck experiment fail before main().
if(VITAMEDIADECK_ENABLE_H264_VITA)
  set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")
  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)
else()
  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
endif()

# Installable homebrew package containing the SELF and application resources.
set(VITAMEDIADECK_VPK_NAME "VitaMediaDeck.vpk")
vita_create_vpk(${VITAMEDIADECK_VPK_NAME} ${VITA_TITLEID} ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE assets/fonts/Inter-Medium.ttf fonts/Inter-Medium.ttf
  FILE assets/fonts/Inter-SemiBold.ttf fonts/Inter-SemiBold.ttf
  FILE assets/fonts/Inter-OFL.txt licenses/Inter-OFL.txt
  FILE LICENSE licenses/VitaMediaDeck-GPLv3.txt
  FILE THIRD_PARTY_NOTICES.md licenses/THIRD_PARTY_NOTICES.md
  FILE release/VitaMediaDeck.spdx licenses/VitaMediaDeck.spdx
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/PROVENANCE.txt" licenses/release-provenance.txt
  FILE licenses/ReAvPlayer-MIT.txt licenses/ReAvPlayer-MIT.txt
  FILE licenses/REAVPLAYER_PROVENANCE.md licenses/REAVPLAYER_PROVENANCE.md
  FILE licenses/quirc-ISC.txt licenses/quirc-ISC.txt
  FILE "${VITAMEDIADECK_STB_ROOT}/LICENSE" licenses/stb-LICENSE.txt
  FILE licenses/libwebp-COPYING.txt licenses/libwebp-COPYING.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/libsmb2-NOTICE.txt" licenses/libsmb2-NOTICE.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/libsmb2-LGPL-2.1.txt" licenses/libsmb2-LGPL-2.1.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/libxml2-MIT.txt" licenses/libxml2-MIT.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/mpg123-COPYING.txt" licenses/mpg123-COPYING.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/FLAC-COPYING.Xiph.txt" licenses/FLAC-COPYING.Xiph.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/libogg-COPYING.txt" licenses/libogg-COPYING.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/vita2d-MIT.txt" licenses/vita2d-MIT.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/FreeType-FTL.txt" licenses/FreeType-FTL.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/libjpeg-turbo-LICENSE.md" licenses/libjpeg-turbo-LICENSE.md
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/libjpeg-turbo-README.ijg" licenses/libjpeg-turbo-README.ijg
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/libpng-LICENSE.txt" licenses/libpng-LICENSE.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/bzip2-LICENSE.txt" licenses/bzip2-LICENSE.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/jansson-MIT.txt" licenses/jansson-MIT.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/pthread-embedded-COPYING.txt" licenses/pthread-embedded-COPYING.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/pthread-embedded-LGPL-2.1.txt" licenses/pthread-embedded-LGPL-2.1.txt
  FILE "${VITAMEDIADECK_RELEASE_LICENSE_ROOT}/pthread-embedded-Vita-MIT.txt" licenses/pthread-embedded-Vita-MIT.txt
  FILE "${VITAMEDIADECK_HTTPS_CURL_ROOT}/share/licenses/curl.txt" licenses/curl.txt
  FILE "${VITAMEDIADECK_HTTPS_CURL_ROOT}/share/licenses/Mbed-TLS.txt" licenses/Mbed-TLS.txt
  FILE "${VITAMEDIADECK_HTTPS_CURL_ROOT}/share/licenses/zlib-header-with-license.txt" licenses/zlib.txt
  FILE "${VITAMEDIADECK_HTTPS_CURL_ROOT}/share/licenses/zstd-package-notice.txt" licenses/zstd.txt
  FILE "${VITAMEDIADECK_HTTPS_PACKAGE}/licenses/certifi-MPL-2.0.txt" licenses/certifi-MPL-2.0.txt
  FILE "${VITAMEDIADECK_LIBSSH2_ROOT}/share/licenses/libssh2-BSD-3-Clause.txt" licenses/libssh2-BSD-3-Clause.txt
  ${VITAMEDIADECK_FFMPEG_LICENSE_RESOURCE}
  FILE "${VITAMEDIADECK_WILIWILI_LICENSE}" licenses/wiliwili-GPL-3.0.txt
  # Third-party MIT taiHEN compatibility module used instead of directly
  # loading SCE_SYSMODULE_AVPLAYER. See avplayer_init.c.
  FILE sce_modules/reAvPlayer.suprx modules/reAvPlayer.suprx
)
add_dependencies(${VITAMEDIADECK_VPK_NAME}-vpk ${PROJECT_NAME}.self-self)
