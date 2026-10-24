# Unified library and global search — 2026-10-24

App version: **1.4.1 / 01.05**. Test identity: **library-1024**.
The frozen 1.4.1 standard and earlier test VPKs are preserved. This follow-up
changes application orchestration; hardware/software decoder repositories,
clock defaults and decoder ABI are unchanged.

## Home and playback continuity

**Home → Library** is now the common entry point for local media, Jellyfin video,
Navidrome music, Immich photos, IPTV, radio and podcasts. **R1** cycles Continue,
History and Sources. **Triangle** opens global search. The six existing Home
categories and each theme's saved order remain available. Sources opens the
existing provider browser/connection management, including Network files.

Continue contains unfinished seekable audio/video with a known playback point.
History also contains completed items, live stations/channels and viewed images.
Live media reopens the live source; it has no invented persistent seek position.
Immich history reopens the selected asset; it is not a video bookmark. Selecting
the item already owned by the mini-player restores that session and its tracks.
Returning from a player retains the Home query, folder/feed and focused identity.

A shared metadata registry uses provider, stable server/account fingerprint and
opaque media identity. Server display names and list positions do not identify an
account. Jellyfin item ids are canonical across folder/search paths; RSS episodes
retain the existing feed/GUID bookmark key even if an enclosure URL changes.
Legacy file/podcast/video bookmarks are imported when their metadata is found.
Metadata refreshes cannot overwrite the active playback alias or a newer rewind.
No synchronization writes are sent to provider servers: this is console-local
state shared by the application's entry points, not cross-device cloud sync.

## Search and caching

One query searches title, filename, artist and album in indexed local media; Jellyfin titles;
Navidrome artists/albums/songs; Immich original filenames; radio/podcast catalogues;
and saved or previously discovered IPTV channels. Artist and album results open
their provider folders; podcast shows open their episode feeds. There is no global
IPTV directory search or semantic/AI image search. New remote content requires a
connected, configured server/catalogue. SMB/SFTP/WebDAV contents already discovered
by browsing are searchable from metadata; global search does not recursively crawl
arbitrary remote filesystem trees.

Cached metadata appears before network requests finish. Three independent workers
publish results without waiting for all providers. A new query cancels the HTTP
work and rejects late replies using its generation. The pool sleeps on a condition
variable when idle. Login-required, loading and unavailable states remain separate;
failed requests retain cached results. Passwords/tokens are copied only into
transient jobs/session storage. Opening results reuses native authentication and
certificate trust confirmation, with the remembered-passwords preference respected.
An unauthenticated worker never prompts or silently accepts a certificate.

Results are grouped by provider; **Square** cycles the provider filter. A global
page is bounded to **24 items per provider / 192 overall**: refine a broad query or
open the provider to browse everything. Navidrome allocates artist/album slots
without crowding out songs. Provider folders keep their existing pagination.
The search follow-up (`search-1024`) prepares merged/sorted snapshots exclusively
on the three low-priority workers. The renderer copies a ready snapshot and
observes a publication revision, including covers arriving after the provider jobs
finish. Two reusable result buffers replace the former 512-result temporary merge
allocation on every UI refresh. Provider quotas are applied before copying cache
records. Idle workers sleep; polling does not restart provider requests.

Local queries read the immutable on-disk metadata index, **eight records per I/O**,
check cancellation before every record (even with no matches), and yield for 1 ms
after every 32 records. No media files are opened or tags re-extracted per query;
the existing catalog indexer supplies title/artist/album tags. File basename
matching applies only to local/network file paths, not opaque provider ids or
credentials in URLs. This bounds individual batches, not total scan time: a
zero-match query can still scan the full local index in the background.

Results retain each theme's native cards, covers, viewport transitions and focus.
The detail prioritizes provider, artist/album and playback duration. A compact
source rail uses radar segments (Ghost Mesh), blocks (Deck 90), diagonal impulses
(Signal), film perforations (Aperture), link nodes (Relay), tiles (Mosaic), or a
simple sweep (Legacy). Source order is Local, Jellyfin, Navidrome, Immich, IPTV,
Radio, Podcast, Network. Ready/loading/login/error have separate states; unavailable
sources retain cache and no indeterminate animation claims a completion percentage.
The header also shows activity; **Reduce Motion** freezes the activity shapes.
Cached content remains usable and incoming batches do not restart item reveals.

The metadata cache retains **512 items**, evicting unplayed cache before the oldest
activity. It is not a second complete copy of every remote library. Search text
uses ASCII case folding with exact UTF-8 matching for other characters.

Incoming results do not restart per-row animations. Focus is anchored by identity;
a cached row about to receive Cross is retained if a refresh would otherwise
replace it. All seven themes use their own existing page geometry, 3D/background
motion, clipped content, input mapping and reduced-motion preference. Ghost Mesh's
right-stick wheel and Aperture's bottom tabs keep their reserved command areas.

Local covers, radio/podcast/IPTV cover URLs and remote primary artwork use the
existing bounded thumbnail/disk cache workers. The Home renderer does not perform
HTTP fetches or decode images. Cover jobs are bounded, deduplicated within the
current query and canceled when leaving it; absent art keeps a provider placeholder.

## Persistence and resource boundaries

`ux0:data/VitaMediaDeck/library.bin` is a framed, CRC-checked journal. Metadata is
written only when changed; playback frames contain a small id/position/duration/
order record. The low-priority checkpoint worker samples confirmed background
positions once per second. Foreground video publishes once per second in memory.
Normal persistence is approximately every 15 seconds; pause, stop and suspend
request an earlier asynchronous checkpoint. No journal I/O runs on the UI/audio
thread. Explicit zero rewind, completion and unknown duration are preserved.

Partial appends retain preceding valid frames. Failed writes/syncs remain dirty
for retry. Compaction uses temporary and backup files plus rename; concurrent
changes are acknowledged only for the revision actually captured. A normal hard
process kill can lose the interval since the last durable checkpoint. The OS may
freeze the application before a suspend callback can finish storage: exact
suspend timing remains a device test, not a zero-loss guarantee.

The previous compact podcast checkpoint remains authoritative for podcast
reopening and continues to use its feed/GUID generation guard. The older history
file remains compatible. The new cache contains private local media metadata and
user-supplied stream/artwork URLs, like existing favourites; provider credentials
and authenticated provider stream URLs are not serialized into it.

## Verification

Run `bash tests/library/run-host-tests.sh` for production cache/checkpoint/fan-out
code under AddressSanitizer/UBSan and ThreadSanitizer. Fixtures exercise real host
threads and filesystem writes, blocked providers, query replacement, immediate
cache results, server/account separation, cache pressure, zero rewind, short-clip
completion, alias rebinding, the production background sampler (including optimistic-seek
rejection), failed writes/syncs and torn-journal recovery.
A virtual hour of one active playback generates about **240 state commits /
16,320 journal bytes** after metadata settles; filesystem metadata and device
power consumption are excluded. Podcasts also retain the existing 56-byte compact
checkpoint (about 13,440 additional payload bytes/hour), independently of this journal.

`bash tests/library/run-ui.sh` renders the actual Home in all seven themes, with
synthetic labelled provider data and mini-player space, and tests progressive
focus and player return. These are host layout images, not Vita screenshots.
The personal-media suite additionally exercises the real HTTP client against a
local fixture server: encoded Jellyfin search parameters, typed results,
Navidrome query encoding, Immich request bodies, canceled HTTP, redirects and
native range-cache regression. Existing UI/motion/provider/podcast suites and the
native Release/LTO VPK audit are part of the build validation.

Recorded host results (library suites refreshed for search-1024; unchanged provider/player suites retain the preceding library-1024 evidence):

| Check | Result |
| --- | --- |
| Library journal/cache | 108 checks under ASan/UBSan and TSan |
| Concurrent global search | 171 checks in the recorded sanitizer runs, including worker-only queries and late cover publication |
| Local immutable index reader | 632 checks under both sanitizer runs: real file records, metadata, I/O/yield budget, zero-match cancellation and corrupt header |
| Production background sampler | 6 checks under both sanitizer runs |
| Unified Home | 424,066 checks, 42 synthetic host layouts, animated/reduced-motion activity and late-cover invalidation |
| Provider APIs / authenticated range cache | 239 API checks / 48,205 byte checks, plus local HTTP search/cancellation |
| Existing personal-media UI | 219,325 checks across seven themes/two languages |
| Game UI / scrolling L1 | 146,670 / 547,063 checks |
| Continuous motion / theme chrome | 225,694 / 19,376 checks |
| Podcast persistence / production MP3 | 143 / 153 checks under both sanitizer runs |
| Documentation | 86 Markdown files, 301 relative links checked |

## Acceptance on PS Vita / real servers

1. Enter Library in every theme. Use R1 for Continue/History/Sources, Triangle for
   search, Square for provider filtering and each theme's established navigation.
   Check Ghost's right stick, Aperture with a mini-player, clipping and text sizes.
2. Play a local MP4/MKV, local music, Jellyfin video, Navidrome track and online/
   downloaded podcast for over 30 seconds. Seek/pause, switch sources, return,
   restart the app and resume. Explicitly rewind to zero and complete an item.
3. Suspend during MP3, AAC/M4A/FLAC, Jellyfin and Navidrome playback. Resume the
   app and, after a process restart, reopen the item. Record the lost interval and
   whether network reconnection is required. Confirm tracks/subtitles when simply
   restoring the still-active mini-player.
4. Search a title, artist, album, podcast and channel. Open artist/album folders,
   episode feeds and paginated provider folders. Return from a result's player to
   the same query/folder. Repeat with one slow/offline server and change the query
   rapidly: other results remain usable and old replies never replace the query.
5. Verify cached results and covers offline, fresh results online, expired sessions,
   remembered-passwords OFF/ON, two accounts/servers with equal titles, renamed/
   removed servers and missing local files. Check RSS GUID continuity and IPTV
   playlists needing a custom User-Agent.
6. Compare UI/player responsiveness and battery/frame timing on the real device.
   Host and build success do not establish these measurements or authenticate to
   the user's servers. Log: `ux0:data/VitaMediaDeck/session_library-1024.txt`.

## API references checked

- [OpenSubsonic search3](https://opensubsonic.netlify.app/docs/endpoints/search3/)
- [Jellyfin primary client search](https://github.com/jellyfin/jellyfin-apiclient-python/blob/master/jellyfin_apiclient_python/api.py)
- [Immich search request definitions](https://github.com/immich-app/immich/blob/main/server/src/dtos/search.dto.ts)

These references support request construction; successful fixtures do not prove
compatibility with every deployed server version. Immich uses the existing
metadata-search contract rather than introducing a server-version migration.
