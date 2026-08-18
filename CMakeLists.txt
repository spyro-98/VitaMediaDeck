## VitaTube CMake project.
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

project(vitatube C)

## Complete public hardware-decoder backend from wiliwili's FFmpeg-vita
## patch.  This is deliberately a whole FFmpeg codec implementation, not the
## old hand-written sequence of individual SceAvcdec calls.  The reproducible
## builder pins both upstream revisions and installs headers/libraries below
## build/deps, keeping generated third-party objects out of git.
option(VITATUBE_ENABLE_H264_VITA
  "Use the complete FFmpeg h264_vita hardware decoder" ON)
if(NOT VITATUBE_ENABLE_H264_VITA)
  message(FATAL_ERROR
    "The packaged VitaTube playback stack requires vita-hw-decoder; "
    "software fallback is selected at runtime, not as a build replacement.")
endif()
set(VITATUBE_H264_VITA_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/ffmpeg-vita-hw"
  CACHE PATH "Prefix produced by tools/build-ffmpeg-vita-hw.sh")

## Standalone packages live in independent sibling repositories by default.
## Override these cache paths when using submodules or an installed layout.
set(VITATUBE_HW_DECODER_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/../vita-hw-decoder"
  CACHE PATH "Path to the vita-hw-decoder repository")
set(VITATUBE_SW_DECODER_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/../vita-sw-decoder"
  CACHE PATH "Path to the vita-sw-decoder repository")
set(VITATUBE_HTTPS_PACKAGE
  "${CMAKE_CURRENT_SOURCE_DIR}/../vita-https"
  CACHE PATH "Path to the vita-https repository")
set(VITATUBE_HTTPS_CURL_ROOT
  "${VITATUBE_HTTPS_PACKAGE}/build/deps/curl-mbedtls"
  CACHE PATH "Pinned curl/Mbed TLS prefix produced by vita-https")
foreach(package_root IN ITEMS
    "${VITATUBE_HW_DECODER_PACKAGE}"
    "${VITATUBE_SW_DECODER_PACKAGE}"
    "${VITATUBE_HTTPS_PACKAGE}")
  if(NOT EXISTS "${package_root}/CMakeLists.txt")
    message(FATAL_ERROR "Missing VitaTube package repository: ${package_root}")
  endif()
endforeach()

set(VITATUBE_LIBSSH2_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/libssh2-vita"
  CACHE PATH "Prefix produced by tools/build-libssh2-vita.sh")
set(VITATUBE_LIBSSH2_ARCHIVE "${VITATUBE_LIBSSH2_ROOT}/lib/libssh2.a")
if(NOT EXISTS "${VITATUBE_LIBSSH2_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITATUBE_LIBSSH2_ARCHIVE}; run tools/build-libssh2-vita.sh before building VitaTube.")
endif()
add_library(vitatube_libssh2 STATIC IMPORTED GLOBAL)
set_target_properties(vitatube_libssh2 PROPERTIES
  IMPORTED_LOCATION "${VITATUBE_LIBSSH2_ARCHIVE}"
  INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto;z")

set(VITATUBE_RELEASE_LICENSE_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/build/release-licenses"
  CACHE PATH "License bundle produced by tools/prepare-release-licenses.sh")
set(VITATUBE_REQUIRED_RELEASE_LICENSES
  libsmb2-NOTICE.txt libsmb2-LGPL-2.1.txt libxml2-MIT.txt
  mpg123-COPYING.txt vita2d-MIT.txt FreeType-FTL.txt
  libjpeg-turbo-LICENSE.md libjpeg-turbo-README.ijg libpng-LICENSE.txt
  bzip2-LICENSE.txt pthread-embedded-COPYING.txt
  pthread-embedded-LGPL-2.1.txt pthread-embedded-Vita-MIT.txt)
foreach(license_file IN LISTS VITATUBE_REQUIRED_RELEASE_LICENSES)
  if(NOT EXISTS "${VITATUBE_RELEASE_LICENSE_ROOT}/${license_file}")
    message(FATAL_ERROR
      "Missing release license ${license_file}; run "
      "tools/prepare-release-licenses.sh before configuring VitaTube.")
  endif()
endforeach()

## The pinned builder enables the real MOV/MP4 demuxer and the complete
## hardware decoder backend. Provenance is documented by the build script.
set(VITATUBE_FFMPEG_ROOT "${VITATUBE_H264_VITA_ROOT}")

set(VITATUBE_AVFORMAT_ARCHIVE "${VITATUBE_FFMPEG_ROOT}/lib/libavformat.a")
if(NOT EXISTS "${VITATUBE_AVFORMAT_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITATUBE_AVFORMAT_ARCHIVE}; run tools/build-ffmpeg-vita-hw.sh before building VitaTube.")
endif()
add_library(vitatube_avformat STATIC IMPORTED GLOBAL)
set_target_properties(vitatube_avformat PROPERTIES
  IMPORTED_LOCATION "${VITATUBE_AVFORMAT_ARCHIVE}")

set(VITATUBE_AVCODEC_ARCHIVE "${VITATUBE_FFMPEG_ROOT}/lib/libavcodec.a")
if(NOT EXISTS "${VITATUBE_AVCODEC_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITATUBE_AVCODEC_ARCHIVE}; run tools/build-ffmpeg-vita-hw.sh before building VitaTube.")
endif()
add_library(vitatube_avcodec STATIC IMPORTED GLOBAL)
set_target_properties(vitatube_avcodec PROPERTIES
  IMPORTED_LOCATION "${VITATUBE_AVCODEC_ARCHIVE}")

set(VITATUBE_AVUTIL_ARCHIVE "${VITATUBE_FFMPEG_ROOT}/lib/libavutil.a")
if(NOT EXISTS "${VITATUBE_AVUTIL_ARCHIVE}")
  message(FATAL_ERROR
    "Missing ${VITATUBE_AVUTIL_ARCHIVE}; run tools/build-ffmpeg-vita-hw.sh before building VitaTube.")
endif()
add_library(vitatube_avutil STATIC IMPORTED GLOBAL)
set_target_properties(vitatube_avutil PROPERTIES
  IMPORTED_LOCATION "${VITATUBE_AVUTIL_ARCHIVE}")
set(VITATUBE_AVUTIL_LINK vitatube_avutil)
set(VITATUBE_FFMPEG_LICENSE
    "${VITATUBE_FFMPEG_ROOT}/share/licenses/FFmpeg-LGPL-2.1.txt")
if(NOT EXISTS "${VITATUBE_FFMPEG_LICENSE}")
  message(FATAL_ERROR
    "Missing ${VITATUBE_FFMPEG_LICENSE}; rerun tools/build-ffmpeg-vita-hw.sh.")
endif()
set(VITATUBE_FFMPEG_LICENSE_RESOURCE
    FILE "${VITATUBE_FFMPEG_LICENSE}" licenses/FFmpeg-LGPL-2.1.txt)
set(VITATUBE_WILIWILI_LICENSE
    "${VITATUBE_FFMPEG_ROOT}/share/licenses/wiliwili-GPL-3.0.txt")
if(NOT EXISTS "${VITATUBE_WILIWILI_LICENSE}")
  message(FATAL_ERROR
    "Missing ${VITATUBE_WILIWILI_LICENSE}; rerun tools/build-ffmpeg-vita-hw.sh.")
endif()

## Without an explicit default, documented `cmake -S/-B` commands produced an
## unoptimized binary. On a 444 MHz Cortex-A9, unoptimized video, UI, audio,
## and media parsing work looks like a functional decoder failure. Use Release by
## default. The unstripped intermediate ELF remains available for
## vita-parse-core independently of this optimization choice.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

## Load VitaSDK's packaging macros after project(). The verified module name is
## vita.cmake, not VitaSDK.cmake.
include("${VITASDK}/share/vita.cmake" REQUIRED)

## --- Application metadata ------------------------------------------------

# Name displayed below the LiveArea bubble.
set(VITA_APP_NAME "VitaTube")

# TITLEID must contain exactly nine characters.
set(VITA_TITLEID  "VTUB00001")

# LiveArea version; vita_create_vpk requires the ##.## format.
set(VITA_VERSION  "01.01")

## --- Compiler and linker flags --------------------------------------------

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=gnu11")
add_link_options("LINKER:-z,noexecstack")

## --- Include paths --------------------------------------------------------

include_directories(
  $<$<BOOL:${VITATUBE_ENABLE_H264_VITA}>:${VITATUBE_H264_VITA_ROOT}/include>
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${VITATUBE_LIBSSH2_ROOT}/include
  ${VITASDK}/arm-vita-eabi/include/libxml2
)

## --- Sources --------------------------------------------------------------

set(VITA_HW_DECODER_FFMPEG_ROOT "${VITATUBE_H264_VITA_ROOT}"
    CACHE PATH "FFmpeg prefix consumed by vita-hw-decoder" FORCE)
set(VITA_SW_DECODER_FFMPEG_ROOT "${VITATUBE_H264_VITA_ROOT}"
    CACHE PATH "FFmpeg prefix consumed by vita-sw-decoder" FORCE)
set(VITA_HTTPS_CURL_ROOT "${VITATUBE_HTTPS_CURL_ROOT}"
    CACHE PATH "curl/Mbed TLS prefix consumed by vita-https" FORCE)
add_subdirectory("${VITATUBE_HW_DECODER_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-hw-decoder")
add_subdirectory("${VITATUBE_SW_DECODER_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-sw-decoder")
add_subdirectory("${VITATUBE_HTTPS_PACKAGE}"
                 "${CMAKE_BINARY_DIR}/packages/vita-https")

# vita-https carries a conservative private -O2 default. Keep VitaTube's
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
  src/media/background_playback.c
  src/media/audio_volume.c
  src/media/avplayer_init.c
  src/media/vita_decoder.c
  src/media/video_thumbnail.c
  src/media/hw_player_screen.c
  src/media/music_metadata.c
  src/history/playback_history.c
  src/settings/preferences.c
  src/system/background_audio.c
  src/system/display_awake.c
  src/system/performance.c
  src/ui/font.c
  src/ui/components.c
  src/ui/runtime.c
  src/ui/focus_glow.c
  src/ui/brand.c
  src/ui/touch.c
  src/ui/text_input.c
  src/ui/loading_screen.c
  src/ui/mini_player.c
  src/ui/sections_sidebar.c
  src/ui/local_media_screen.c
  src/ui/music_player.c
  src/ui/settings_screen.c
  src/ui/about_screen.c
  src/ui/log_viewer.c
  src/ui/text_reader.c
  src/ui/network_sources_screen.c
)

# The software H.264 path is CPU-bound on the three user cores of the Vita.
# Keep the final application and the vendored FFmpeg LTO objects in one link-
# time optimization unit. GCC treats -O4 exactly like -O3; -O3 is the highest
# distinct standard optimization level and is already selected by Release.
target_compile_options(${PROJECT_NAME} PRIVATE
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -flto=4 -fuse-linker-plugin
  -ffunction-sections -fdata-sections)
target_link_options(${PROJECT_NAME} PRIVATE
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  -flto=4 -fuse-linker-plugin
  -Wl,--gc-sections)

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
  ## is reduced from the official sample to symbols used by VitaTube. JPEG and
  ## PNG thumbnails are decoded by their software libraries, not SceJpeg.
  vita2d
  ScePgf_stub         # sceFont* weak imports used by libvita2d system PGF CJK fallback
  jpeg                # vita2d image decoder dependency
  ## libpng depends on zlib; both are required by the PNG header/logo path.
  freetype            # Inter TTF rasterization at exact native UI sizes
  bz2                 # FreeType static dependency
  png
  z
  VitaHwDecoder::VitaHwDecoder
  VitaSwDecoder::VitaSwDecoder
  VitaHttps::VitaHttps
  vitatube_avformat   # pinned FFmpeg with a registered MOV/MP4 demuxer
  vitatube_avcodec    # complete h264_vita plus software H.264 fallback
  ${VITATUBE_AVUTIL_LINK}
  mpg123              # local MP3 playback
  vitatube_libssh2    # authenticated SFTP with pinned host fingerprints
  smb2                # authenticated SMB2/SMB3 shares
  xml2                # namespace-safe WebDAV PROPFIND parser
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
  SceAudio_stub       # sceAudioOut* used by the audio-drain thread
  SceDisplay_stub     # repeated for vita2d's later one-pass references
  m                   # sinf/cosf used by vita2d
  pthread             # FFmpeg and libcurl worker support
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
## budget that made an earlier VitaTube experiment fail before main().
if(VITATUBE_ENABLE_H264_VITA)
  set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")
  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)
else()
  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
endif()

# Installable homebrew package containing the SELF and application resources.
set(VITATUBE_VPK_NAME "VitaTube.vpk")
vita_create_vpk(${VITATUBE_VPK_NAME} ${VITA_TITLEID} ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/vitatubelogoalpha.png sce_sys/vitatubelogoalpha.png
  FILE assets/fonts/Inter-Medium.ttf fonts/Inter-Medium.ttf
  FILE assets/fonts/Inter-SemiBold.ttf fonts/Inter-SemiBold.ttf
  FILE assets/fonts/Inter-OFL.txt licenses/Inter-OFL.txt
  FILE LICENSE licenses/VitaTube-GPLv3.txt
  FILE THIRD_PARTY_NOTICES.md licenses/THIRD_PARTY_NOTICES.md
  FILE release/VitaTube.spdx licenses/VitaTube.spdx
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/PROVENANCE.txt" licenses/release-provenance.txt
  FILE licenses/ReAvPlayer-MIT.txt licenses/ReAvPlayer-MIT.txt
  FILE licenses/REAVPLAYER_PROVENANCE.md licenses/REAVPLAYER_PROVENANCE.md
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/libsmb2-NOTICE.txt" licenses/libsmb2-NOTICE.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/libsmb2-LGPL-2.1.txt" licenses/libsmb2-LGPL-2.1.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/libxml2-MIT.txt" licenses/libxml2-MIT.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/mpg123-COPYING.txt" licenses/mpg123-COPYING.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/vita2d-MIT.txt" licenses/vita2d-MIT.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/FreeType-FTL.txt" licenses/FreeType-FTL.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/libjpeg-turbo-LICENSE.md" licenses/libjpeg-turbo-LICENSE.md
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/libjpeg-turbo-README.ijg" licenses/libjpeg-turbo-README.ijg
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/libpng-LICENSE.txt" licenses/libpng-LICENSE.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/bzip2-LICENSE.txt" licenses/bzip2-LICENSE.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/pthread-embedded-COPYING.txt" licenses/pthread-embedded-COPYING.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/pthread-embedded-LGPL-2.1.txt" licenses/pthread-embedded-LGPL-2.1.txt
  FILE "${VITATUBE_RELEASE_LICENSE_ROOT}/pthread-embedded-Vita-MIT.txt" licenses/pthread-embedded-Vita-MIT.txt
  FILE "${VITATUBE_HTTPS_CURL_ROOT}/share/licenses/curl.txt" licenses/curl.txt
  FILE "${VITATUBE_HTTPS_CURL_ROOT}/share/licenses/Mbed-TLS.txt" licenses/Mbed-TLS.txt
  FILE "${VITATUBE_HTTPS_CURL_ROOT}/share/licenses/zlib-header-with-license.txt" licenses/zlib.txt
  FILE "${VITATUBE_HTTPS_CURL_ROOT}/share/licenses/zstd-package-notice.txt" licenses/zstd.txt
  FILE "${VITATUBE_HTTPS_PACKAGE}/licenses/certifi-MPL-2.0.txt" licenses/certifi-MPL-2.0.txt
  FILE build/deps/libssh2-vita/share/licenses/libssh2-BSD-3-Clause.txt licenses/libssh2-BSD-3-Clause.txt
  ${VITATUBE_FFMPEG_LICENSE_RESOURCE}
  FILE "${VITATUBE_WILIWILI_LICENSE}" licenses/wiliwili-GPL-3.0.txt
  # Third-party MIT taiHEN compatibility module used instead of directly
  # loading SCE_SYSMODULE_AVPLAYER. See avplayer_init.c.
  FILE sce_modules/reAvPlayer.suprx modules/reAvPlayer.suprx
)
add_dependencies(${VITATUBE_VPK_NAME}-vpk ${PROJECT_NAME}.self-self)
