#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
version="${VITAMEDIADECK_RELEASE_VERSION:-snapshot}"
output="${1:-$repo_root/build/release/VitaMediaDeck-${version}-Corresponding-Source.tar.gz}"
stage="$(mktemp -d "${TMPDIR:-/tmp}/vitamediadeck-source.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$(dirname "$output")" "$stage/projects" "$stage/third-party"

snapshot_worktree() {
  local source="$1" name="$2" list
  list="$(mktemp "${TMPDIR:-/tmp}/vitamediadeck-files.XXXXXX")"
  git -C "$source" ls-files --cached --others --exclude-standard | \
    while IFS= read -r path; do [[ -e "$source/$path" ]] && printf '%s\n' "$path"; done > "$list"
  mkdir -p "$stage/projects/$name"
  tar -C "$source" -cf - -T "$list" | tar -C "$stage/projects/$name" -xf -
  rm -f "$list"
}

snapshot_worktree "$repo_root" VitaMediaDeck
snapshot_worktree "${VITAMEDIADECK_HW_DECODER_PACKAGE:-$repo_root/../vita-hw-decoder}" vita-hw-decoder
snapshot_worktree "${VITAMEDIADECK_SW_DECODER_PACKAGE:-$repo_root/../vita-sw-decoder}" vita-sw-decoder
snapshot_worktree "${VITAMEDIADECK_HTTPS_PACKAGE:-$repo_root/../vita-https}" vita-https

copy_tree() {
  local source="$1" target="$2"
  [[ -d "$source" ]] || { echo "Missing corresponding source: $source" >&2; exit 1; }
  mkdir -p "$stage/third-party/$target"
  cp -R "$source"/. "$stage/third-party/$target/"
}

copy_tree "$repo_root/build/release-sources" general
copy_tree "$repo_root/build/release-licenses" general-licenses
copy_tree "$repo_root/build/deps/ffmpeg-vita-hw/share/sources" ffmpeg-hardware
copy_tree "${VITAMEDIADECK_SW_DECODER_PACKAGE:-$repo_root/../vita-sw-decoder}/build/deps/ffmpeg-vita-sw/share/sources" ffmpeg-software
copy_tree "$repo_root/build/deps/libssh2-vita/share/sources" libssh2
copy_tree "${VITAMEDIADECK_HTTPS_PACKAGE:-$repo_root/../vita-https}/build/deps/curl-mbedtls/share/sources" https

cp "$repo_root/release/DEPENDENCIES.lock" "$stage/DEPENDENCIES.lock"
cp "$repo_root/release/VitaMediaDeck.spdx" "$stage/VitaMediaDeck.spdx"
cp "$repo_root/LICENSE" "$stage/VitaMediaDeck-GPL-3.0.txt"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$stage/THIRD_PARTY_NOTICES.md"
tar -C "$stage" -czf "$output" .
tar -tzf "$output" >/dev/null
echo "$output"
