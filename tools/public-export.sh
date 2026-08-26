#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
version="${VITAWAVE_EXPORT_VERSION:-snapshot}"
output="${1:-$repo_root/build/public/VitaWave-${version}-source.tar.gz}"
mode="${2:---head}"
mkdir -p "$(dirname "$output")"

case "$mode" in
  --head)
    [[ -z "$(git -C "$repo_root" status --porcelain)" ]] || {
      echo "Refusing HEAD export from a dirty tree" >&2; exit 1;
    }
    git -C "$repo_root" archive --format=tar \
      --prefix="VitaWave-${version}/" HEAD | gzip -n > "$output"
    ;;
  --worktree)
    list="$(mktemp "${TMPDIR:-/tmp}/vitawave-export.XXXXXX")"
    trap 'rm -f "$list"' EXIT
    git -C "$repo_root" ls-files --cached --others --exclude-standard | \
      while IFS= read -r path; do [[ -e "$repo_root/$path" ]] && printf '%s\n' "$path"; done > "$list"
    tar -C "$repo_root" -czf "$output" -T "$list"
    ;;
  *) echo "Usage: $0 [output.tar.gz] [--head|--worktree]" >&2; exit 2 ;;
esac

if tar -tzf "$output" | grep -Eq '(^|/)(\.git|psvitaframe\.png|libav(codec|format)\.a)$'; then
  echo "Refusing export containing private metadata or forbidden binary assets" >&2
  exit 1
fi
echo "$output"
