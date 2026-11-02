# Standalone package integration

VitaMediaDeck consumes five independent repositories. The default layout keeps
them side by side:

```text
personale/
├── VitaMediaDeck/
├── vita-hw-video-decoder/
├── vita-sw-video-decoder/
├── vita-hw-audio-decoder/
├── vita-sw-audio-decoder/
└── vita-https/
```

The paths can instead be supplied during configuration:

```sh
cmake -S . -B build \
  -DVITAMEDIADECK_HW_DECODER_PACKAGE=/absolute/path/vita-hw-video-decoder \
  -DVITAMEDIADECK_SW_DECODER_PACKAGE=/absolute/path/vita-sw-video-decoder \
  -DVITAMEDIADECK_HW_AUDIO_PACKAGE=/absolute/path/vita-hw-audio-decoder \
  -DVITAMEDIADECK_SW_AUDIO_PACKAGE=/absolute/path/vita-sw-audio-decoder \
  -DVITAMEDIADECK_HTTPS_PACKAGE=/absolute/path/vita-https
```

## What VitaMediaDeck links

```cmake
add_subdirectory("${VITAMEDIADECK_HW_AUDIO_PACKAGE}" ...)
add_subdirectory("${VITAMEDIADECK_SW_AUDIO_PACKAGE}" ...)
add_subdirectory("${VITAMEDIADECK_HW_DECODER_PACKAGE}" ...)
add_subdirectory("${VITAMEDIADECK_SW_DECODER_PACKAGE}" ...)
add_subdirectory("${VITAMEDIADECK_HTTPS_PACKAGE}" ...)

target_link_libraries(vitamediadeck PRIVATE
  VitaHwAudioDecoder::VitaHwAudioDecoder
  VitaSwAudioDecoder::VitaSwAudioDecoder
  VitaHwVideoDecoder::VitaHwVideoDecoder
  VitaSwVideoDecoder::VitaSwVideoDecoder
  VitaHttps::VitaHttps)
```

The four decoder libraries have distinct public targets and symbols. Video modules contain video/presentation/session control only; they consume the header-only audio driver contract. Native AAC/MP3 lives in the HW audio archive, FFmpeg AAC/AC-3/MP3 in the SW audio archive. One app-owned `src/media/audio_playback.c` engine supplies workers, PCM queue, volume, AudioOut and the clock to both video backends.

The app sets `audio_driver` in all HW/SW open configs. Track replacement and seek remain transactions in the video session owner, including readiness gates and rollback. Subtitle extraction and retained audio/subtitle language choices remain in the app adapter. A standalone video module with `audio_driver=NULL` opens video only.

This is video API 3.0.0 and audio API 1.0.0; rebuild consumers. Existing video remotes keep their old GitHub addresses until renamed by the owner. The new audio repositories are `spyro-98/vita-hw-audio-decoder` and `spyro-98/vita-sw-audio-decoder`. Development uses the `main` branches; `release/DEPENDENCIES.lock` pins exact component commits. Push all four component branches before running CI for the app revision. Historical local test packages retain their original source-hash manifests.

## Runtime selection

`src/media/vita_decoder.c` owns selection, not either package:

1. create fresh audio/video cursors and open `vita-hw-video-decoder`;
2. if hardware open fails, destroy it and open `vita-sw-video-decoder`;
3. if `h264_vita` fails later while receiving the first access units, close
   the hardware session and reopen software near the last media timestamp;
4. expose one neutral status to the existing playback screen, including the
   active HW/SW badge.

This keeps fallback policy in the application while each decoder package stays
deterministic and reusable.

## HTTP(S) ownership

VitaMediaDeck calls `vita_https_init()` before network-source initialization and
`vita_https_shutdown()` at application teardown. Jellyfin authentication,
library browsing, and poster retrieval use `vita_https_perform()`. Jellyfin
direct play uses authenticated `HEAD` and bounded Range requests. Its access
token stays in the session credential structure and is never serialized.
`vita-https` remains HTTPS-only by default, but Jellyfin explicitly enables its
HTTP mode for `http://` and bare port-8096 sources. HTTPS requests cannot
redirect down to HTTP.
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

## 1.4.1 reproducible baseline

App 1.4.1 uses package APP_VER 01.05 and pins decoder/HTTPS revisions in
`release/DEPENDENCIES.lock`. Decoder ABI versions remain 2.0.0. Both app decoder
targets link the same patched FFmpeg n6.0 prefix selected by the root CMake file;
the SW package's standalone FFmpeg 8.1.2 builder is not the VPK's linked FFmpeg.

The 27 October IPTV correction adds a reviewed `h264_vita` lifecycle patch to
VitaMediaDeck's FFmpeg builder and fixes timestamps in the app's HLS remux.
The codec patch prevents duplicate initialization and stale resource releases;
it benefits every app source using that HW codec. Jellyfin's authenticated range
reader is separate from the HLS remux. These app-owned changes are not implicitly
inherited by either decoder package's standalone builder. See the
[correction and user-confirmed console result](../docs/iptv-hw-startup-2026-10-27.md).

Standard options: empty `VITAMEDIADECK_TEST_ID`, performance clock profile,
`VITA_SW_VIDEO_DECODER_PLANAR_UPLOAD=OFF`, both CPU-thread cache options at 3. The HW
backend remains hardware-only; the thread option is not three hardware decode
engines. Forward-only SW input always selects one worker.

App-owned subtitle state, media resume, UI themes/QR and network provider setup
are not moved into decoder packages. The new continuous UI motion and utility
coverage do not alter decoder APIs, libraries or default clock policy.

The current development integration extracts codec implementations into the
separate HW/SW audio libraries and keeps the shared playback engine in the app.
Video backend selection is independent: software AC-3 audio does not force
software H.264 video.

## Audio and video selection

Settings → Playback stores independent AUTO/HW/SW defaults. Each decoder row
shows the current supported codec set. HW audio is AAC/MP3 mono/stereo; SW audio
is AAC/MP3/AC-3 with stereo output. AUTO tries native audio first and may use SW
without changing the selected video policy. Explicit HW/SW audio never silently
changes the audio backend. Preferences use previously unused V4 extension bits,
leaving existing settings and migrations intact.

The video player's R1 audio selector lists all three modes and their formats.
The app reopens at the current position with the selected tracks and pause/volume
state; a rejected mode rolls back before changing the saved default. The same
default is passed to background video/mini-player playback. Pure music/radio
format routing remains in the existing dedicated audio player.
