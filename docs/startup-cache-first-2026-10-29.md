# Cache-first startup — 29 October 2026

The retained console logs from this morning report 9,185 and 19,840 ms for the
combined history/artwork/library stage, versus 63 and 34 ms for network setup.
They do not isolate the cost of each local operation. Source inspection confirms
that the animated loader waited for every operation before opening Home; it did
not wait for Wi-Fi association. These are earlier logs, not a new device run.

Home now restores its local playback registry and starts its existing search
workers without awaiting network initialization or artwork directory discovery.
Network setup starts independently while UI assets load. Artwork discovery has
its own low-priority worker and publishes one entry at a time, with no directory
I/O under the shared index lock. Existing background Home refresh remains active.

Newly cached images also get an atomic, hash-keyed reference to their validated
cache basename. Worker reads can restore those images without joining discovery
or downloading them again. Existing cache files remain supported by discovery;
persisted artwork paths remain usable immediately. Render lookups remain
memory-only. Invalid references cannot select a different key or escape the cache.

The Wi-Fi slot now draws muted bars while disconnected or before network setup,
separate from the activity indicator. HTTPS publishes completed runtime state
with acquire/release ordering before status/client workers can use NetCtl/curl.
Shutdown joins startup workers before transport teardown.

Schema-3 diagnostics time history, library replay, search, network and artwork
separately. ASan/UBSan and TSan fixtures hold network and discovery indefinitely
while local startup completes, and hold cache storage I/O while a cold keyed
image read and render peek succeed without a fetch. HTTPS publication, artwork
restart/eviction, the real Home loop, and status layouts across all eight themes
also pass. After installing the test VPK, the user reported substantially better
startup on the physical console. No numeric startup benchmark was captured.
Version 1.4.3 packages this correction with updated release metadata.
