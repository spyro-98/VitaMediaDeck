#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
allow_dirty=0
check_history=0
vpk=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --allow-dirty) allow_dirty=1 ;;
    --check-history) check_history=1 ;;
    --vpk) shift; vpk="${1:?missing VPK path}" ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

fail() { echo "release-audit: $*" >&2; exit 1; }
require_file() { [[ -f "$1" ]] || fail "missing $1"; }

cd "$repo_root"
if [[ $allow_dirty -eq 0 ]] && [[ -n "$(git status --porcelain)" ]]; then
  fail "working tree is dirty; commit intentionally or use --allow-dirty for local validation"
fi

for forbidden in sce_sys/psvitaframe.png vendor/ffmpeg-vita/libavcodec.a \
                 vendor/ffmpeg-vita/libavformat.a; do
  if git ls-files --error-unmatch "$forbidden" >/dev/null 2>&1; then
    fail "forbidden tracked artifact: $forbidden"
  fi
done

if git grep -InE 'BotGuard|innertube|youtubei/v1|AIza[0-9A-Za-z_-]{20,}' -- \
    src CMakeLists.txt assets sce_sys >/dev/null 2>&1; then
  fail "current source snapshot contains retired platform or credential markers"
fi

if [[ $check_history -eq 1 ]]; then
  if git tag --list | grep -q .; then
    fail "existing repository has private/archive tags; publish a clean export, not git history"
  fi
  if git log --all --format='%H %s' | grep -Eiq 'BotGuard|Innertube|YouTube'; then
    fail "existing history contains retired integration work; publish a clean export"
  fi
fi

require_file release/DEPENDENCIES.lock
require_file release/VitaMediaDeck.spdx
require_file licenses/REAVPLAYER_PROVENANCE.md
require_file "$repo_root/build/release-licenses/PROVENANCE.txt"
printf '%s  %s\n' \
  8b6d26c97b7e9a3b24ae69d2eadf1f6e7223102dcc375b9ca30e466815a04850 \
  sce_modules/reAvPlayer.suprx | shasum -a 256 -c - >/dev/null

https_root="${VITAMEDIADECK_HTTPS_PACKAGE:-$repo_root/../vita-https}"
curl_root="${VITAMEDIADECK_HTTPS_CURL_ROOT:-$https_root/build/deps/curl-mbedtls}"
require_file "$curl_root/lib/libcurl.a"
nm_tool="${VITASDK:?Set VITASDK before the release audit}/bin/arm-vita-eabi-gcc-nm"
require_file "$nm_tool"
nm_output="$(mktemp "${TMPDIR:-/tmp}/vitamediadeck-curl-nm.XXXXXX")"
trap 'rm -f "$nm_output"' EXIT
"$nm_tool" -u "$curl_root/lib/libcurl.a" > "$nm_output"
grep -q 'mbedtls_' "$nm_output" || fail "release curl is not linked to Mbed TLS"
if grep -Eq 'OPENSSL_|SSL_(CTX|connect|read|write)' "$nm_output"; then
  fail "release curl still references OpenSSL"
fi

libssh2_root="${VITAMEDIADECK_LIBSSH2_ROOT:-$repo_root/build/deps/libssh2-vita}"
libssh2="$libssh2_root/lib/libssh2.a"
require_file "$libssh2"
"$nm_tool" -u "$libssh2" > "$nm_output"
grep -q 'mbedtls_' "$nm_output" || fail "release libssh2 is not linked to Mbed TLS"
if grep -Eq 'OPENSSL_|SSL_(CTX|connect|read|write)' "$nm_output"; then
  fail "release libssh2 still references OpenSSL"
fi

jansson_root="${VITAMEDIADECK_JANSSON_ROOT:-$repo_root/build/deps/jansson-vita}"
jansson="$jansson_root/lib/libjansson.a"
require_file "$jansson"
if "$VITASDK/bin/arm-vita-eabi-readelf" -r "$jansson" |
   grep -Eq 'R_ARM_BASE_PREL|R_ARM_GOT_BREL'; then
  fail "release Jansson archive contains Vita-incompatible PIC relocations"
fi

ffmpeg_root="${VITAMEDIADECK_H264_VITA_ROOT:-$repo_root/build/deps/ffmpeg-vita-hw}"
require_file "$ffmpeg_root/lib/libavformat.a"
"$nm_tool" -g --defined-only "$ffmpeg_root/lib/libavformat.a" > "$nm_output"
grep -q 'ff_mov_demuxer' "$nm_output" || fail "release FFmpeg misses MOV/MP4 demuxing"
grep -q 'ff_matroska_demuxer' "$nm_output" || fail "release FFmpeg misses Matroska demuxing"
grep -q 'ff_srt_demuxer' "$nm_output" || fail "release FFmpeg misses SRT demuxing"

for license in libsmb2-NOTICE.txt libsmb2-LGPL-2.1.txt libxml2-MIT.txt mpg123-COPYING.txt \
  vita2d-MIT.txt FreeType-FTL.txt libjpeg-turbo-LICENSE.md \
  libjpeg-turbo-README.ijg libpng-LICENSE.txt bzip2-LICENSE.txt jansson-MIT.txt \
  pthread-embedded-COPYING.txt \
  pthread-embedded-LGPL-2.1.txt pthread-embedded-Vita-MIT.txt; do
  require_file "$repo_root/build/release-licenses/$license"
done

if [[ -n "$vpk" ]]; then
  require_file "$vpk"
  unzip -t "$vpk" >/dev/null
  listing="$(unzip -Z1 "$vpk")"
  grep -qx 'licenses/Mbed-TLS.txt' <<<"$listing" || fail "VPK misses Mbed TLS license"
  grep -qx 'licenses/certifi-MPL-2.0.txt' <<<"$listing" || fail "VPK misses CA license notice"
  grep -qx 'licenses/VitaMediaDeck.spdx' <<<"$listing" || fail "VPK misses SPDX SBOM"
  grep -qx 'licenses/release-provenance.txt' <<<"$listing" || fail "VPK misses dependency provenance"
  if grep -qx 'sce_sys/psvitaframe.png' <<<"$listing"; then
    fail "VPK still contains the unlicensed PS Vita frame"
  fi
fi

echo "release-audit: source snapshot, TLS backend, provenance and licenses passed"
