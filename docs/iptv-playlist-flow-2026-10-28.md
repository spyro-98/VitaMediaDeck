# Guided M3U playlists and spatial navigation — 28 October 2026

The former generic Add stream page combined playlist and playback types, save
shortcuts and a linear focus order. The IPTV entry point now names the task:
**Add M3U playlist → URL or QR → Add and open → saved channels**. A new list
is fetched and parsed before it is saved; unresolvable HTML and empty/invalid lists never create a saved playlist.
Static HTML redirects and unique M3U links use the shared bounded resolver. Extensionless HTTP(S) URLs are treated as
playlists in this flow. Import currently accepts remote URLs, including QR;
this update does not add a local-file import picker.

The empty collection has one prominent Add action and short guidance. The form
hides the irrelevant stream-type selector and separates the primary action from
**Open without saving**. Successful addition returns to the saved collection on
Back, selecting the new list. Open-once returns to its draft form. Storage
failure keeps the URL in the form with a visible failure message, allowing the
primary action to retry. Adding uses a host-based name without a second keyboard;
an existing saved name is preserved, and Options provides Rename afterwards.
The URL field has a label, explicit edit affordance, URL placeholder and native
captions rendered above the PAF panel. After QR, focus goes to Add and open.
**Open a direct stream** remains a separate Options action, with its type selector.
The Italian and English in-app guide describes this exact flow.

The listing has a single rail: Add, Search/count and Options, all reachable with
arrows/Cross and touch. The duplicate contextual row above cards is removed.
Cross/touch opens a playlist or plays the selected channel; save, collection
switching and filters are organized in R1. Saved-channel status is not repeated
as a disabled button or per-card caption in its own collection. Existing
category routing is preserved.

Shared grid navigation now moves down into a partial last row, choosing its
nearest available column. Left/right remain within the visible row. The fix is
used by local files, library and category collections, remote browsing and IPTV.
Aperture no longer exits content early when the next row is incomplete. The
folder destination picker includes its Use/Create/Back footer in D-pad focus;
the network source editor includes its visible Cancel action. Labels retain the
controller-icon gutters and shared spacing from the preceding update, with a
larger IPTV footer reserve.

## Verification

- `tests/navigation/run-host-tests.sh`: 18,941,085 assertions, grids from every
  starting cell, partial rows and IPTV form/header/card geometry.
- `tests/iptv/run-ui-tests.sh`: existing production UI/layout checks plus
  production playlist-flow checks in eight themes and IT/EN, using arrows/Cross
  without requiring Start or a shoulder-button shortcut. Negative cases cover
  cancellation, bad data and failed storage; open-once never persists.
- `tests/ui_crossfade/run-motion.sh`: folder picker footer selection,
  cache/parent/mkdir/cancel regressions and all eight theme scenes.
- Shared crossfade/settings/guide, themed online flows, native UI routing,
  game/L1 interaction and actual main browse routing suites passed.

Host PNGs use substituted rendering; no new network/playback backend behaviour
is inferred from them. Release/LTO compilation, final PAF linked-import audit,
VPK license audit and packaged asset checks accompany the dated delivery under
`build/releases/1.4.1/iptv-flow-20261028`. This package uses the standard app
identity and includes the previous controller-icon/spacing corrections. The
preceding controller-icon package is retained separately. Source changes remain
uncommitted. Physical input, native PAF appearance and performance on this build
still require a Vita session.
