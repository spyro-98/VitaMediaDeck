# Restore media tabs while retiring only the old Home — 27 October 2026

The user clarified the regression in standard a6d6dd4: removing the obsolete
aggregate Home also bypassed the Video, Music, Images and offline-podcast category
screens. They opened the bare filesystem browser, which has no category tab rail.
The tab definitions were still present; their category UI had become unreachable.

The main browser again calls the existing indexed category screen for those tabs.
Its per-theme tab rail, cached folder/flat views, ordering, storage filters,
Navidrome/Immich shortcuts and provider transitions are preserved. File browsing
is opened only by an explicit filesystem/folder action. Back from a folder goes
back to its category, and playback returns to the same selected category tab.
Offline-podcast Back retains the established transition to online podcasts.

Only HOME_LIBRARY uses the integrated startup dashboard. An entry guard prevents
the category screen from drawing the retired aggregate Home, and selecting Home
in its tab rail hands control back to the dashboard before another category
frame is drawn. L1 Home and Back from a category also return to that dashboard.
An explicit filesystem action originating in the dashboard returns there.
Restoring the grid exposed a second VMD-only outline outset in the shared
grid focus path. It is now counted once, so the top border stays inside the
three-pixel gutter and the bottom border fits above the mini-player. The
existing pixel-border test and Legacy/VMD geometry sweep cover this correction.

All six tabs and saved per-theme ordering remain intact. No stored preferences
or provider configuration are migrated or rewritten by this correction.

The app icon in the shared Legacy/VMD status header now uses the full 54-pixel
bar height, preserving aspect ratio; breadcrumb placement and its L1 touch area
follow the new bounds. Other themes retain their custom L1 command markers.
The darker app icon asset and the full-screen wallpaper are unchanged.

## Verification

`python3 tests/library/test-browse-navigation.py --negative-ref a6d6dd4`
extracts and compiles the **actual complete browse_local function** with real
app type/action headers and scripted screen, playback and filesystem boundaries.
It checks dashboard -> category -> folder -> category, playback return, explicit
Files, category -> Home, direct dashboard file browsing and online/offline
podcasts. The current route passes 274 ASan/UBSan assertions; the preceding route
fails with normal exit 1 at the first attempted Video-category entry. These
scripts verify orchestration, not real filesystem contents, Sony pixels or input
inside the mocked screens. The full source files compile separately for ARM.

The existing all-theme tab/input/persistence, Home/search, chrome and Home-rail
host suites run before the standard build. ARM Release/LTO, linked PAF imports,
standard identity, license and corresponding-source audits remain required.
No emulator, firmware or ARM JIT is executed. Existing text/search/player fixes
remain integrated. Delivery to USB does not install or run the build.

## Vita acceptance

1. From the startup dashboard select each tab: Video, Music, Radio, Podcast and
   Images. The category tab UI must remain available, with its existing theme.
2. Enter a local media folder and press Circle: return to that category, with
   its tabs. Play a file and close the player: return to that category.
3. Select Home from the category rail or L1: see the integrated dashboard. The
   old grid aggregating local Video/Music/Images must never appear as Home.
4. Check category grid/list and R1 filters, Navidrome/Immich shortcuts, offline
   podcasts and saved tab ordering. Verify Down from tabs still enters item one.
5. Check the 54-pixel status icon, adjacent captions and touching its full area
   to open L1, with and without the mini-player. Sony display acceptance remains
   a physical-console test.
