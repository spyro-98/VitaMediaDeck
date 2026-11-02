# AC-3 / MP3 video-audio regression

Run with pinned FFmpeg 6 libraries configured for AC-3/MP3 and swresample:

```sh
VMD_HOST_FFMPEG=/path/to/ffmpeg6 bash tests/multiaudio/run-host-tests.sh
```

The generated six-second 720p H.264 MKV contains AAC, center-only AC-3 5.1,
44.1-kHz stereo MP3, 22.05-kHz mono MP3 and 11.025-kHz mono MP3. The shared app
engine executes its real audio producer, output queue and teardown.
Vita system decoder/kernel/AudioOut calls are host substitutes. The tests observe
system MP3 selection and MPEG version, plus init/allocation/create failure
fallback to software audio. They do not execute Vita silicon.

Checks cover audible center-channel downmix, decoder/resampler drain, timestamps,
codec-specific 576/1024/1152/1536 output grains, absence of per-packet silence gaps,
seek/reopen, gated startup, pause/resume and cancellation. Partial grains are
padded only once at clean EOF. ASan/UBSan cover the codec modules and shared playback engine.
The separate playback-seek adapter test verifies that AAC/AC-3/MP3 track changes
and rollback retain the chosen video backend. The audio-volume suite also checks
all four output grain sizes with mono/stereo boost and mute.

The software path uses the standard FFmpeg send/receive API and
[swresample conversion/downmix](https://ffmpeg.org/doxygen/6.0/group__lswr.html).
System AAC/MP3 uses the public
[VitaSDK audio decoder contract](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/audiodec.h).
Physical Vita MP3/AC-3 output, A/V sync and sustained 720p performance remain
separate device checks; host audio and native package builds do not establish them.

The current split tests the single app-owned output engine and the independent
HW/SW audio libraries. `session_test.c` compiles each actual video session owner
with that engine and codec modules: it exercises AAC/AC-3/MP3 replacement,
rollback after a rejected candidate, bidirectional seek, retained language
snapshots, pause/volume and cursor cleanup. Video presentation and Vita firmware
are substituted in this test. `tests/decoder` separately tests the production
video pipeline; `tests/playback_seek` checks subtitle/language retention in the
production app adapter and subtitle reader.

`tests/decoder_packages/run-native-checks.sh` also builds/installs all four
packages and compiles a consumer using only the installed exported targets.
Its symbol audit rejects audio implementations in the video archives and
verifies that the app compiles exactly one playback/output engine.

The AUTO/HW/SW matrix covers all five generated tracks. It asserts that forced
SW never creates a native decoder, forced HW does not fall back after native
failure (or for AC-3), and AUTO can recover from native AAC/MP3 initialization
errors. The software module now also exercises AAC decoding and EOF drain.
