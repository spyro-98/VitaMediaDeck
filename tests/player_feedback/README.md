# Browser return and seek preview regressions

```sh
VMD_HOST_FFMPEG=/path/to/configured/ffmpeg bash tests/player_feedback/run-host-tests.sh
```

Requires Clang, pkg-config libpng/libjpeg and configured host FFmpeg headers.
Includes the production thumbnail, mini-player and video-player implementations;
platform graphics/input calls are substituted. ASan/UBSan cover:

- Busy thumbnail cancellation across suspend/resume, stale generations, repeated
  resume preserving priority cancellation, failed GPU upload retry and handoff.
- Returning from fullscreen to the same browser by Start or the mini-player title.
- Frozen seek preview RGB565 channels, padded framebuffer and texture strides,
  preview drawing/cleanup, allocation/read errors, initial open and failed seek.

GPU visibility, actual frame capture and seek timing still need a Vita.
