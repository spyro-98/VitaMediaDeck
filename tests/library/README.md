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

Run `bash tests/library/run-ui.sh` for 56 host-rendered layouts of the production
Home, integrated dashboard components/empty state, spatial reachability, direct
saved-connection selection, carousel item changes, all themes, cached covers, mini-player space, progressive focus/return,
late-cover refresh, partial/empty loading, and Reduce Motion checks. Synthetic titles/artwork are test fixtures, not user data or screenshots
from PS Vita. Existing provider UI tests stub the library service independently.

Real-transport provider request/encoding/cancellation fixtures are in the existing
`tests/personal_media/run-host-tests.sh` suite. See
[implementation and console checks](../../docs/unified-library-2026-10-24.md).

The real worker fixture also distinguishes explicit favourites/starred IPTV from
merely discovered media, replaces removed favourites and retains cached covers.
The dashboard uses bounded metadata and does not trigger provider-wide refreshes
until search or an actual saved connection is opened.

The saved-connection contract suite extracts the production direct-entry function
and checks rejected/accepted SSH/TLS trust, failed pin persistence, authentication
failure, restored browser navigation and section propagation using host adapters.
It does not authenticate to real accounts.

Home also covers cached files without playback history, fair provider sampling,
all collection-availability combinations and individual visible server-row touch
targets. The UI script renders 72 synthetic host references across eight themes.

The search fixture distinguishes POSIX thread registration from raw Vita kernel
creation: it rejects the latter, checks the explicit 256 KiB stack and low native
priority, and requires all three workers to reach their initial idle wait before
submitting work. It also checks partial/total creation failure and matching joins.
The previous raw-thread mock silently supplied POSIX registration, which could
not expose the Vita pthread-embedded cancellation/TLS incompatibility. See the
[startup investigation](../../docs/startup-search-threads-2026-10-26.md).

Run `python3 tests/library/test-browse-navigation.py` for the actual main
browser loop with scripted screen/playback/filesystem boundaries. It checks
that category tabs remain reachable, folder/playback return retains the category,
Home opens only the integrated dashboard, and offline podcasts retain their
online return. `--negative-ref a6d6dd4` verifies the preceding category bypass
fails with normal exit 1. This is routing evidence, not real-screen input or
Sony rendering; see the [category correction](../../docs/category-tabs-2026-10-27.md).

## Local image access independent of indexing

Run `python3 tests/library/test-local-images.py --negative-ref e84de5b` for the
actual category entry functions, with substituted index/root records. It checks
empty/populated folder and flattened catalogues, the permanent Local images
entry, Immich ordering, typed open actions and unchanged other categories under
ASan/UBSan. The negative source exits normally with status 1. Run
`python3 tests/library/test-browse-navigation.py` for the actual app router,
including image-filtered device browsing, viewer return and category retention.
These are host contracts, not Vita screenshots or hardware input tests.
