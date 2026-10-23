# Crossfade native layout checks

Part of the 1.4.1 validation set. See [suite index and evidence boundaries](../../docs/VALIDATION.md).

Run `bash tests/ui_crossfade/run-host-tests.sh` from the repository.

The harness uses the production components, focus, settings, controls guide,
About, file-action modal, audio player, download view and preferences code.
ASan/UBSan cover the host run. FreeType/PNG substitute platform drawing and
emit 960×544 images in the printed temporary directory for visual inspection.
No images from these fixtures belong in the release README.

- Finite focus settle, interruption, reduced motion, clock jumps and draw budget.
- Audio layouts, missing covers, persisted selection and paused-player safety.
- English/Italian settings with/without mini-player, tab/content ownership,
  translated guide chapters, About wrapping and Cancel-first deletion.
- Download known/unknown totals, paused/running states, bytes beyond 2 GiB,
  foreground header-loader separation and success/error feedback.

The platform header, actual playback, subtitle renderer, camera and storage
are substituted. Native build and VPK integrity checks remain separate, as do
Vita runtime, camera recognition, frame timing and concurrent streaming tests.

Additional suites: `tests/iptv/run-host-tests.sh` for production IPTV/catalog
navigation and transport regressions; `tests/home_tabs/test.c` for all 720 tab
orders. Thumbnail/artwork requests stay bounded by the actual visible layout.

`bash tests/ui_crossfade/run-game-ui.sh` adds native material/animation and
Legacy L1 checks, independently varying live player/download availability.
It covers provider entry mapping, touch/button dispatch, noninteractive group
headings, reduced-motion determinism, animated pixel changes, and a bounded
geometry budget. Its PNG sequence uses synthetic content and host rendering;
it does not establish console FPS or authenticated server behavior.

The game suite checks the Ghost perspective mesh: regular 60-degree nodes,
opposite centers, four GPU triangle batches, bounded vertex count, continuous
subpixel movement and frame pacing arithmetic. The local summary fixture
checks the worker's typed counts, large file totals and representative artwork.
`run-theme-flows.sh` also runs held-worker transition tests: leaving never
joins unfinished I/O, stale replies cannot replace the new list, failed refresh
keeps cached data, and every compact media row requests its thumbnail.

## Continuous page transitions

`run-host-tests.sh` also runs `page_motion_test.c` against the production physics.
`run-motion.sh` renders browser/Home reference frames for all seven themes with
rapid reversals, cache append and touch during command-deck motion. It checks
bounded overscan, shared transforms and non-overlapping hit regions. Set
`VMD_MOTION_OUTPUT` to choose its output directory. These are synthetic host
fixtures, not Vita GPU timing or screenshots.

## Guide and current-focus help

The page fixture also compares guide headings with localized chapter roles,
checks current-theme and saved normal/swapped mapping prefixes, rejects a tiny
output buffer, and exercises index-to-text, scroll, return and exit in IT/EN
with and without mini-player for every theme. Hint widths are checked against
the actual Inter and Deck 90 Mono metrics, including the HUD/panel rails.
Reference `guide-home-*` images show the active theme and scrolled chapter.
