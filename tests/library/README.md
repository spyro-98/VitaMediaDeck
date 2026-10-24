# Unified library regression tests

Run `bash tests/library/run-host-tests.sh` for the production catalog/checkpoint
engine and real three-worker search pool under ASan/UBSan and TSan. Filesystem
and timing are host adapters; no real server credentials or console are needed.
The fan-out fixture intentionally ignores cancellation on one blocked server to
prove late-generation replies are rejected even when a transport cannot abort.

The local-index suite extracts the actual production reader and its header, then
reads real binary records through POSIX adapters: title/filename/artist/album,
eight-record reads, 32-record yield budget, cancellation without any match, and
corrupt headers. The search fixture asserts that every cache query runs on a
worker, and checks publication of covers after provider completion.

Run `bash tests/library/run-ui.sh` for 42 host-rendered layouts of the production
Home, all themes, cached covers, mini-player space, progressive focus/return,
late-cover refresh, partial/empty loading, and Reduce Motion checks. Synthetic titles/artwork are test fixtures, not user data or screenshots
from PS Vita. Existing provider UI tests stub the library service independently.

Real-transport provider request/encoding/cancellation fixtures are in the existing
`tests/personal_media/run-host-tests.sh` suite. See
[implementation and console checks](../../docs/unified-library-2026-10-24.md).
