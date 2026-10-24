# Podcast resume host regressions

Run `bash tests/podcast_resume/run-host-tests.sh` with clang, Python, ffmpeg and
host libmpg123 (`pkg-config libmpg123`). No account, network or console is used.
The fixtures live in a fresh temporary directory, including all `ux0:` paths.

The persistence harness compiles the production history and podcast saver.
Real host files, fsync, rename and pthreads exercise restart, suspend/resume
callback dispatch on the saver thread, unknown duration, completed/zero points,
failed history import, generation checks, partial writes, failed write/rename
retry, CRC rejection, backup/temp recovery, and storage ownership. A virtual
one-hour playback checks the periodic payload budget and absence of volume
sync. Address/undefined and thread sanitizers run independently.

The codec harness extracts the exact production `play_mp3` function and its
reader adapters at test time, then compiles them with real libmpg123 and host
file/audio adapters. This avoids unrelated Vita GPU/AvPlayer headers; it does
not reimplement the decoder loop. Generated 60-second CBR/VBR audio exercises
local and stream readers, restart at 30 seconds, failed initial seek, paused
rewind, and failed audio output before/after confirmed playback.

The sampler in the persistence fixture is synthetic. Console kernel callback
delivery, SD latency, suspended network sessions, Vita libmpg123/AvPlayer, M4A/AAC
and FLAC output still require the [Vita checklist](../../docs/podcast-resume-2026-10-24.md).
The byte budget counts payload only; filesystem metadata is additional.
