#!/usr/bin/env bash
set -euo pipefail
vmd_repo="$(cd "$(dirname "$0")/../.." && pwd)"
vmd_jansson="${VMD_HOST_JANSSON:?Set a host Jansson prefix (include and lib)}"
vmd_output="$(mktemp -d "${TMPDIR:-/tmp}/vmd-online-audio-check.XXXXXX")"
cd "$vmd_repo"
read -r -a vmd_cflags <<<"$(pkg-config --cflags libmpg123 openssl libxml-2.0)"
read -r -a vmd_libs <<<"$(pkg-config --libs libmpg123 openssl libxml-2.0)"
vmd_flags=(-O1 -g -Wall -Wextra -Isrc -I../vita-https/include
    -Itests/online_audio/include "-I$vmd_jansson/include" "${vmd_cflags[@]}")
vmd_link=("$vmd_jansson/lib/libjansson.a" -lpthread "${vmd_libs[@]}")
ffmpeg -hide_banner -loglevel error -f lavfi -i sine=frequency=440:sample_rate=44100 \
    -t 2 -ac 2 -c:a libmp3lame -b:a 128k "$vmd_output/tone.mp3"
for vmd_sanitizer in address,undefined thread; do
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/online_audio/test.c \
        src/media/live_audio.c src/network/online_audio.c src/network/http_audio_stream.c \
        src/network/artwork_cache.c "${vmd_link[@]}" -o "$vmd_output/audio-test"
    vmd_fixture="$(mktemp -d "$vmd_output/audio.XXXXXX")"
    (cd "$vmd_fixture"; "$vmd_output/audio-test" "$vmd_output/tone.mp3")
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/online_audio/cache_test.c \
        src/network/iptv_catalog.c src/network/iptv_manifest.c \
        src/network/iptv_saved.c "${vmd_link[@]}" -o "$vmd_output/cache-test"
    vmd_fixture="$(mktemp -d "$vmd_output/cache.XXXXXX")"
    (cd "$vmd_fixture"; "$vmd_output/cache-test")
    clang "${vmd_flags[@]}" -ffunction-sections -fdata-sections "-fsanitize=$vmd_sanitizer" \
        tests/online_audio/query_cache_test.c src/network/online_audio.c "${vmd_link[@]}" \
        -Wl,-dead_strip -o "$vmd_output/query-cache-test"
    vmd_fixture="$(mktemp -d "$vmd_output/query-cache.XXXXXX")"
    (cd "$vmd_fixture"; "$vmd_output/query-cache-test")
    clang "${vmd_flags[@]}" "-fsanitize=$vmd_sanitizer" tests/online_audio/prefetch_test.c \
        -lpthread -o "$vmd_output/prefetch-test"
    "$vmd_output/prefetch-test"
done
printf 'Online audio host test artifacts: %s\n' "$vmd_output"
