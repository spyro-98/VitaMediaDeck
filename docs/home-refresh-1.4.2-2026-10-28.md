# Reactive Home — VitaMediaDeck 1.4.2, 28 October 2026

Home used to retain carousel anchors between visits and could append an old
focused item back into a refreshed collection after it disappeared. Its 250 ms
poll gate also delayed actual registry/publication changes. Recent playback
excluded every item already in Continue, making a new partially watched video
appear not to have changed Recently played. Saved favourites were a worker cache
with no direct invalidation from their storage commits.

## Current behaviour

- Every Home entry resets old carousel anchors to the current leading items.
  The focused section remains selected, and active collection navigation keeps
  an item's identity only while it remains present. Refresh never resurrects a
  removed favourite or a completed/expired Continue item.
- Continue and Recently played query the authoritative in-memory registry.
  Recent includes partially watched media too; Continue retains the existing
  ten-second/start and near-end completion policies. Completed media can remain
  in playback history but are not presented as resumable.
- Registry/publication changes are consumed on the next Home loop rather than
  being held behind the 250 ms pending-work polling interval. Pending workers
  still use the throttled status poll; this does not cause continuous full scans.
- Successful online favourite, IPTV saved-entry and saved-source commits publish
  atomic revisions. Failed writes do not announce a change. They do not change
  existing JSON/binary formats or erase data.
- The search coordinator observes saved revisions and queues a bounded local
  metadata job without starting another search generation or doing provider
  requests. Home-entry saved jobs precede local discovery. A failed saved-file
  read retains the last valid publication and retries after five seconds.
- Freshly appended favourites are read first. During the session the changed
  online/IPTV provider is promoted, retaining that priority across Home visits.
  Only explicit online favourites and starred IPTV entries enter Favourites.
- Saved connection metadata is rebuilt from the current saved sources on entry
  and after a successful source revision change. New appended connections appear
  first. The event-based local read occurs before drawing, with no remote fetch;
  source/credential buffers are replaced only after a valid new source load.
- Existing artwork caches and background cover loading remain in use. Header
  and footer text in the moving L1 drawer now share its screen-edge scissor, as
  verified by the Home animation bounds regression.

## Evidence

`tests/library/home_refresh_test.c` drives the production Home loop and registry
in all eight themes and both mini-player heights. Changes occur after the first
rendered frame: play/favourite/source changes are reflected and selectable within
1–3 fixture frames, inside the old 250 ms gate, without leaving Home. Re-entry
with a stale anchor selects the latest media; completion and favourite removal do
not reinsert the old item. These are deterministic host fixtures, not a measured
Vita response-time claim.

`tests/library/search_test.c` exercises the real worker pool and saved snapshots:
add/update/remove while the same Home generation remains active, publication
updates, changed IPTV priority, cancellation and cover/cache behaviour under
ASan/UBSan and TSan. The production registry tests cover progress, completion,
restart, checkpoints and concurrent I/O ownership.

The online and IPTV storage regressions assert that failed publication leaves
the revision unchanged and successful changes advance it. The source regression
extracts the actual production source-DB prefix (not a copied implementation)
and verifies add/edit/remove plus failed fsync/rename rollback under ASan/UBSan
and TSan. The existing full Home suite renders its current collections, layouts,
focus, drawer animation, mini-player space and player return across all themes.

Build/archive/metadata/native-import/LTO audits accompany the released VPK.
Physical console testing and transfer remain unverified while USB is unavailable.
