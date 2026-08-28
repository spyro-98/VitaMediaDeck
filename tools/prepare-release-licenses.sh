#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
output="${VITAMEDIADECK_RELEASE_LICENSE_ROOT:-$repo_root/build/release-licenses}"
sources="${VITAMEDIADECK_RELEASE_SOURCE_ROOT:-$repo_root/build/release-sources}"
work="$(mktemp -d "${TMPDIR:-/tmp}/vitamediadeck-licenses.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$output" "$sources"

fetch() {
  local url="$1" file="$2" sha256="$3"
  curl -L --fail --retry 3 --max-time 300 "$url" -o "$file"
  printf '%s  %s\n' "$sha256" "$file" | shasum -a 256 -c -
}

fetch https://github.com/sahlberg/libsmb2/archive/refs/tags/libsmb2-6.1.tar.gz \
  "$work/libsmb2.tar.gz" cee0a62457a46b1a4d48b9b0094cf53f489e3ff5028d1344c188156aeb1c7239
fetch https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.3.tar.xz \
  "$work/libxml2.tar.xz" 78262a6e7ac170d6528ebfe2efccdf220191a5af6a6cd61ea4a9a9a5042c7a07
fetch https://www.mpg123.de/download/mpg123-1.33.5.tar.bz2 \
  "$work/mpg123.tar.bz2" 0d7ebc8da0aff3ca383c8c6b5a6adbe402ee5bb256685b8c5499f3a739f9d6dd
fetch https://download-mirror.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz \
  "$work/freetype.tar.xz" 36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
fetch https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.0/libjpeg-turbo-3.2.0.tar.gz \
  "$work/libjpeg.tar.gz" 6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e
fetch https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.58.tar.gz \
  "$work/libpng.tar.gz" a9d4df463d36a6e5f9c29bd6f4967312d17e996c1854f3511f833924eb1993cf
fetch https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz \
  "$work/bzip2.tar.gz" ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
fetch https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.xz \
  "$work/zlib.tar.xz" d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3
fetch https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz \
  "$work/zstd.tar.gz" eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3

for archive in "$work"/*.tar.*; do tar -xf "$archive" -C "$work"; done
install -m 0644 "$work/libsmb2-libsmb2-6.1/COPYING" "$output/libsmb2-NOTICE.txt"
install -m 0644 "$work/libsmb2-libsmb2-6.1/LICENCE-LGPL-2.1.txt" "$output/libsmb2-LGPL-2.1.txt"
install -m 0644 "$work/libxml2-2.15.3/Copyright" "$output/libxml2-MIT.txt"
install -m 0644 "$work/mpg123-1.33.5/COPYING" "$output/mpg123-COPYING.txt"
install -m 0644 "$work/freetype-2.14.3/docs/FTL.TXT" "$output/FreeType-FTL.txt"
install -m 0644 "$work/libjpeg-turbo-3.2.0/LICENSE.md" "$output/libjpeg-turbo-LICENSE.md"
install -m 0644 "$work/libjpeg-turbo-3.2.0/README.ijg" "$output/libjpeg-turbo-README.ijg"
install -m 0644 "$work/libpng-1.6.58/LICENSE" "$output/libpng-LICENSE.txt"
install -m 0644 "$work/bzip2-1.0.8/LICENSE" "$output/bzip2-LICENSE.txt"
install -m 0644 "$work/libsmb2.tar.gz" "$sources/libsmb2-6.1.tar.gz"
install -m 0644 "$work/libxml2.tar.xz" "$sources/libxml2-2.15.3.tar.xz"
install -m 0644 "$work/mpg123.tar.bz2" "$sources/mpg123-1.33.5.tar.bz2"
install -m 0644 "$work/freetype.tar.xz" "$sources/freetype-2.14.3.tar.xz"
install -m 0644 "$work/libjpeg.tar.gz" "$sources/libjpeg-turbo-3.2.0.tar.gz"
install -m 0644 "$work/libpng.tar.gz" "$sources/libpng-1.6.58.tar.gz"
install -m 0644 "$work/bzip2.tar.gz" "$sources/bzip2-1.0.8.tar.gz"
install -m 0644 "$work/zlib.tar.xz" "$sources/zlib-1.3.2.tar.xz"
install -m 0644 "$work/zstd.tar.gz" "$sources/zstd-1.5.7.tar.gz"

git clone --quiet https://github.com/xerpi/libvita2d.git "$work/libvita2d"
git -C "$work/libvita2d" checkout --quiet a8f15ab09d5233f0a4e4ad0e8f6ade0da888cbed
install -m 0644 "$work/libvita2d/LICENSE" "$output/vita2d-MIT.txt"
git -C "$work/libvita2d" archive --format=tar.gz \
  --prefix=libvita2d-a8f15ab0/ -o "$sources/libvita2d-a8f15ab0.tar.gz" HEAD
git clone --quiet https://github.com/vitasdk/pthread-embedded.git "$work/pthread"
git -C "$work/pthread" checkout --quiet 11d2e5722d98c86f33c908fc47b2cf6e55205db5
install -m 0644 "$work/pthread/COPYING" "$output/pthread-embedded-COPYING.txt"
install -m 0644 "$work/pthread/COPYING.LIB" "$output/pthread-embedded-LGPL-2.1.txt"
install -m 0644 "$work/pthread/COPYING.vita" "$output/pthread-embedded-Vita-MIT.txt"
git -C "$work/pthread" archive --format=tar.gz \
  --prefix=pthread-embedded-11d2e572/ \
  -o "$sources/pthread-embedded-11d2e572.tar.gz" HEAD
git clone --quiet https://github.com/SonicMastr/ReAvPlayer.git "$work/ReAvPlayer"
git -C "$work/ReAvPlayer" checkout --quiet abff24a21ffec3e57fc1cd3b1d17dc26666251b0
git -C "$work/ReAvPlayer" archive --format=tar.gz \
  --prefix=ReAvPlayer-abff24a2/ -o "$sources/ReAvPlayer-abff24a2.tar.gz" HEAD

cat > "$output/PROVENANCE.txt" <<'EOF'
libsmb2.version=6.1
libsmb2.source.sha256=cee0a62457a46b1a4d48b9b0094cf53f489e3ff5028d1344c188156aeb1c7239
libxml2.version=2.15.3
libxml2.source.sha256=78262a6e7ac170d6528ebfe2efccdf220191a5af6a6cd61ea4a9a9a5042c7a07
mpg123.version=1.33.5
mpg123.source.sha256=0d7ebc8da0aff3ca383c8c6b5a6adbe402ee5bb256685b8c5499f3a739f9d6dd
freetype.version=2.14.3
freetype.source.sha256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
libjpeg-turbo.version=3.2.0
libjpeg-turbo.source.sha256=6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e
libpng.version=1.6.58
libpng.source.sha256=a9d4df463d36a6e5f9c29bd6f4967312d17e996c1854f3511f833924eb1993cf
bzip2.version=1.0.8
bzip2.source.sha256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
zlib.version=1.3.2
zlib.source.sha256=d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3
zstd.version=1.5.7
zstd.source.sha256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
vita2d.commit=a8f15ab09d5233f0a4e4ad0e8f6ade0da888cbed
pthread-embedded.commit=11d2e5722d98c86f33c908fc47b2cf6e55205db5
ReAvPlayer.commit=abff24a21ffec3e57fc1cd3b1d17dc26666251b0
EOF

echo "Release license bundle is ready at $output"
