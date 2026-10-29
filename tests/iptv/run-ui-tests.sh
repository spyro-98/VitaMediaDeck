#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
vmd_output="${VMD_IPTV_UI_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/vmd-iptv-flow.XXXXXX")}"
mkdir -p "$vmd_output"
read -r -a vmd_cflags <<<"$(pkg-config --cflags freetype2 libpng)"
read -r -a vmd_libs <<<"$(pkg-config --libs freetype2 libpng)"
for vmd_case in ${VMD_IPTV_UI_CASES:-ui_test channel_sort_test playlist_flow_test programme_flow_test identity_flow_test local_import_flow_test player_saved_panel_test player_panel_test quality_flow_test channel_preferences_test playback_failure_test saved_membership_test cached_artwork_test cached_audio_test browser_layout_test browser_scroll_test};do
 vmd_extra=(-DSCE_CTRL_HEADPHONE=8192)
 if [ "$vmd_case" = player_saved_panel_test ];then
  read -r -a vmd_av_flags <<<"$(pkg-config --cflags libavutil)"
  vmd_extra=(-DSCE_CTRL_HEADPHONE=8192 -Itests/player_feedback/seek/include -Itests/player_feedback/thumbnail/include "${vmd_av_flags[@]}")
 fi
 clang -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -ffunction-sections -fdata-sections \
  -Isrc -I../vita-https/include -Itests/iptv/include -Itests/online_navigation/include -Itests/online_audio/include "${vmd_extra[@]}" \
  "-I$(xcrun --show-sdk-path)/usr/include/libxml2" "${vmd_cflags[@]}" \
  tests/iptv/$vmd_case.c src/network/iptv_manifest.c src/network/iptv_catalog.c \
  "${vmd_libs[@]}" -lxml2 -Wl,-dead_strip -o "$vmd_output/$vmd_case"
 "$vmd_output/$vmd_case" assets/fonts/Inter-Medium.ttf "$vmd_output"
done
printf 'IPTV flow host previews: %s\n' "$vmd_output"
