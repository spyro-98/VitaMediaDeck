# Offline podcast checks

Part of the 1.4.1 validation set. See [suite index and evidence boundaries](../../docs/VALIDATION.md).

Run `VMD_HOST_JANSSON=/path/to/host-jansson bash tests/podcast_metadata/run-host-tests.sh`.
The prefix must contain `include/jansson.h` and `lib/libjansson.a` for the host.
`VMD_HOST_FLAC` defaults to Homebrew's FLAC prefix.

The ASan/UBSan harness uses the production sidecar writer and the regular music
metadata loader with POSIX substitutes for Vita file APIs. All mutations are in
a new temporary directory. Checks cover cache eviction/offline loading, Unicode,
episode/show/author/description/duration, all five cover formats, hidden siblings,
final filenames with spaces and duplicate suffixes, move/collision/delete,
missing or cancelled artwork, write failure cleanup, malformed/untrusted JSON,
oversized covers, orphan-companion collisions and legacy music metadata.

Native compilation and these host checks do not prove Vita rendering or real
network transfers. On device, download an episode without embedded ID3 artwork,
turn Wi-Fi off, restart the app, and open it from local media. Check its cover,
title/author/show and Cover player theme; then rename/move it and repeat.
