# Pinned h264_vita lifecycle regression

Run `VMD_HOST_FFMPEG=/path/to/configured/host/ffmpeg6 bash tests/h264_vita_lifecycle/run-host-tests.sh`.
The default archive is the exact source shipped with the installed Vita FFmpeg
dependency; override `VMD_FFMPEG_VITA_SOURCE` to test the staged source archive.

The harness compiles the actual pinned `vitadec_video.c`, real FFmpeg H.264
SPS/PPS parsing and host libavcodec/libavutil. Only Vita memory/decoder APIs
are replaced with explicit ownership counters. It checks in-band SPS/PPS with
zero initial extradata, existing global metadata, packet NEW_EXTRADATA,
repeated metadata, dimension changes, partial allocation/API failures and
idempotent cleanup (18 cases under ASan/UBSan). A reverse-patched negative
control must fail with normal exit 1, without executing firmware or crashing.

The host declarations model the fields consumed by this source; they are not
32-bit Vita ABI validation. No test decodes a frame on SceVideodec, proves
first-frame success, measures power or simulates the console's decoder timing.
Use the real ARM toolchain and physical Vita acceptance separately.
