# Provider navigation and favourite removal — 24 October 2026

Follow-up to app 1.4.1 / APP_VER 01.05. Decoder source and package versions are
unchanged. The separate verification build uses test ID `providers-1024`;
the frozen standard 1.4.1 artifact is retained.

## Behaviour

- Radio and podcast favourites can be toggled with Square or the explicit R1
  Add/Remove favourite action from Home, saved lists and discovery. The selected
  item's marker and menu label follow its saved state. A resolved podcast keeps
  its catalogue ID, so the same show can also be removed from search results.
- The saved store removes the exact kind + URL, writes transactionally, preserves
  unrelated favourites and metadata, and retains an interrupted-commit backup
  until the replacement succeeds. Corrupt input is not overwritten. Removing
  a favourite does not delete downloaded episodes, stop playback or change a
  remote server. A failed write leaves the visible list unchanged.
- Home caches are invalidated after mutation. Pending saved-list refresh/cover
  replies cannot resurrect a removed item. Selection clamps to the next/previous
  surviving item; the last removal keeps the empty favourites view.
- Discovery uses dedicated Radio/Podcast tabs in every theme. Up from the first
  result row focuses those tabs; left/right switches provider and Down/X enters
  content. Home wheel/section routing remains Home-only. Context menus name the
  next action and expose Back.
- Country/genre results, shows and episode lists retain nested parent snapshots,
  selected item, scroll position and breadcrumb labels. Failed/cancelled fetches
  do not replace the parent. Four extra snapshots plus the direct parent bound
  memory; snapshots are released when resetting the hierarchy or changing provider.
- IPTV Options prioritises Open/Play, explicit Add/Remove favourite, Rename and
  Remove saved entry. Playlist-specific actions name the current list. The saved
  browser exposes All/Favourites, while availability tools stay inside playlists.
  Circle returns one level even with a search active and restores the saved-list
  position. Unstarring does not jump to the first item. Deleting a saved entry
  remains separately confirmed.
- IPTV cover cancellation is nonblocking. Completion is published only for the
  current page generation; the pending worker retains its own item until joined.
  Catalogue requests use a separate cancellation flag.
- Navidrome/Immich connections use R1 Options for Connect, Add server, Edit and
  Remove connection. A fresh X confirms removing the local reference; Circle
  cancels. This does not add remote favourite writes, uploads or remote deletion.
- Theme chrome and distinct content layouts remain active; the changes apply to
  Legacy, Ghost Mesh, Deck 90, Signal, Aperture, Relay and Mosaic. IT/EN on-screen
  copy, the in-app guide, README and controls reference describe these routes.

## Validation and remaining console checks

Recorded host checks passed:

- 706,785 themed-flow checks: seven themes, IT/EN, removal through the real R1
  input path, add/remove in discovery, empty last-item state, failed writes,
  Home/provider input separation and retained player returns.
- 25,998 cache-transition checks, including cancelled pending IPTV artwork,
  stale completion rejection and cleanup at app shutdown.
- 43 navigation-transaction assertions, including nested parents, bounded
  history, failed/cancelled loads and country-filter reset.
- Online-audio ASan/UBSan and TSan suites: 6,096 assertions per main run, exact
  kind/URL removal, retained metadata, failed commit rollback and backup recovery;
  artwork/catalogue caches and audio prefetch regressions also pass.
- IPTV host suites, including persistence, availability, source/session/VOD,
  2,998 UI/navigation checks and 9,709 catalogue layout checks.
- Personal-media API/real local HTTP/range checks, plus 148,556 UI checks:
  remove, cancel and persistence failure in every theme for both services.
- Shared focus/motion/audio/settings/guide/download/Home suites, utility-screen
  coverage (including all 196 QR combinations), and relative Markdown links.

The provider-title overlap discovered in Ghost Mesh/Deck 90 host previews was
corrected and the affected previews inspected again. The renderer keeps native
theme colours/effects; no new media codec or decoder-performance claim is made.


Production-screen input/render harnesses, persistence fault tests and native
Release/LTO compilation accompany this build. Exact final results and the VPK
hash are recorded in its local verification bundle; host images are not Vita
captures or timing measurements.

On Vita, check Square/R1 removal from Home and discovery for radio and podcasts,
last-item removal, then restart the app to verify persistence. Open country →
show → episodes and press Circle repeatedly: each list and selection should
return. In every theme, provider navigation must not change Home sections.
For IPTV, test All/Favourites, unstar, confirmed saved-entry deletion and Back
from a filtered playlist. Switch pages with uncached artwork and verify no pause
waiting for its network request. With Navidrome/Immich, cancel and confirm local
connection removal, then verify another saved server still connects. Repeat with
mini-player/download active and reduced motion enabled. Real provider accounts,
controller feel and physical display acceptance remain console checks.
