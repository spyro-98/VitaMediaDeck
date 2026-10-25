# Complete Legacy Vita theme / Sony PAF — 25 October 2026

**Later console evidence:** the next build passed native initialization but
crashed after a widget-preparation error. See the [second crash, factory and
recovery correction](paf-widget-recovery-2026-10-25.md). Native rendering has not
yet received physical acceptance.

**Console correction:** the first physical selection crashed during Framework
creation because the 8 MiB global heap consumed the 9 MiB dialog partition.
See the [dump diagnosis and correction](paf-crash-home-2026-10-25.md).
The heap now uses application memory and graphics limits are explicit. The
original host results below did not validate native initialization on firmware.

Legacy Vita is the eighth selectable theme. It preserves Legacy layout and
navigation while the shared presentation backend supplies actual Sony PAF
Text, Button, Plane and BusyIndicator widgets throughout the app. It is not a
Sony-looking skin drawn solely through the previous renderer and is not an
L1-only integration. The existing seven theme IDs and default Legacy remain.

## Coverage

| Screen or layer | Integration |
| --- | --- |
| Home, history, continue, global search and progressive results | Shared native text, panels, action buttons and focus; Legacy layout and bounds |
| Local/remote files, folders, destination picker and file actions | Native shared labels, panels, actions and focus; original thumbnails/paths/input |
| Jellyfin, Navidrome, Immich, IPTV, radio, podcasts and details | Same shared presentation, including options, favourites and connection forms |
| L1 sections and audio-layout page, R1 context menus | Native buttons/text, existing animated translation, scrolling and hit targets |
| Settings, tab-order editor, guide, About, log list/reader | Native shared text and controls, including less-used utility pages |
| Downloads, empty/error/success screens and loading | Native labels/panels/actions and firmware busy indicator; reduced-motion fallback stays stationary |
| QR scanner and URL/text entry backdrop | Native controls around the unchanged camera crop; system IME retains its own rendering |
| Audio/video HUD, mini-player and image information/actions | Native shared controls, text, focus and opacity; decoder/image pixels retain their GPU path |
| Subtitles | Media content retains the selected font, size, placement and outline; theme rendering does not replace these preferences |

Coverage is established from shared source call paths and host tests. It is not
a claim of successful Sony rasterization on a physical Vita. Artwork, pictures,
video frames, camera pixels, geometric decoration and existing seek/progress
tracks keep their existing renderer. Actual Sony widgets supply the interface;
this does not replace the media pipeline or recreate the entire Sony shell.

## Rendering and lifetime

`src/ui/native_vita.c` loads firmware PAF on the first frame that selects Legacy
Vita, before starting the GXM scene. Initialization never calls AppUtil init or
CommonDialog configuration and never replaces an already-owned PAF framework.
OneFrame/CommonDialog mode keeps the app event loop and uses vita2d's actual
render target, stride and sync object after its GXM scene ends. Other dialog
updates are forwarded normally; low-level system IME stays independent.

The CommonDialog-mode contract, firmware style resolution, text visibility
window and child-order behavior still require real-firmware acceptance. Public
headers expose `Framework::Update(void *)`; compilation and mocked calls cannot
prove the interpretation of that parameter at runtime.

UI drawing records bounded commands and copies labels. Persistent typed pools
reuse up to 160 text widgets, 64 buttons, 48 planes and four busy indicators.
There are at most 512 presentation commands per frame. Text/geometry/style and
paint order are updated only when changed. The firmware rasterizes its own
system face within measured app label bounds, using clipping and ellipsis.
The native font can differ slightly from the previous Inter metrics.

Clipping follows the app viewport. Later opaque backgrounds trim/hide previous
native labels; translucent scrims reduce opacity. Media drawing removes native
decoration in overlapping artwork/camera regions, retaining the immediate
background. A partly occluded label retains its largest rectangular visible
window. Partially visible button targets keep their immediate decoration while
moving through the clipped boundary. This avoids delayed native controls
painting over media or through a later modal. These edge cases are console
acceptance items, not pixel-identical rendering guarantees.

After the native page is hidden there are no further native update calls until
native UI is submitted again. Hidden busy indicators stop. Fullscreen playback
with no HUD and only subtitle media text therefore does not continually update
an empty native page. Detected module/constructor/widget/frame-target failures
fall back to app drawing for the process. Native heap/framework lifetime remains
process-wide after first use; theme switching hides the page rather than
unloading firmware under other system services.

Module initialization requests an 8 MiB heap, with a 2 MiB surface pool and 1 MiB
text-surface pool in framework parameters. These are configured limits, not
measurements of total RAM/CDRAM, frame time or battery. Media decoding, decoder
ABI, upload format and clock policy are unchanged. The existing per-theme tab
ordering and grid/list preference have an independent eighth slot.

Hold **L1 + R1 while starting the app** to persist and recover to Legacy if the
selected native theme cannot start. Default installations still start Legacy.

## Reproducibility and licensing

PAF code/assets are loaded from the user's firmware; none is distributed. Own C
bindings use ABI facts from ScePaf-RE at
`b76d6ae00516b107d37e7e8a1a16211e3b729a07`. No recovered SDK header is copied into
the source or VPK. The development reference was checked with ARM GCC 15.2,
including sizes 124/180/148/44/16/12 for Framework, its parameters, plugin
parameters, page parameters, ID and string. Relevant ARM offsets and all 30
imports are recorded/audited. Inspection of the linked Vita ELF confirms all
30 functions in four weak firmware libraries; no strong PAF import is introduced.

The original `assets/paf/legacy_vita.xml` contains symbolic references to firmware
common styles/textures, not extracted Sony assets. Its embedded RCO is 1,728
bytes, SHA-256 `7577eaa4f0b3c87b73ae5a776cd85502a0109b3980d5263569af5fcbd916ce40`.
Compile it with GPL-3.0 psp2cxml-tool, commit
`d971a9a9892c122b4edef15d335326b2a551fec4`:

```sh
python3 tools/regenerate-native-vita.py --compiler /path/to/psp2cxml-tool
python3 tools/regenerate-native-vita.py --compiler /path/to/psp2cxml-tool --check
python3 tools/audit-native-vita.py --elf /path/to/build/vitamediadeck.velf
bash tests/native_paf/run-host-tests.sh
```

The compiler uses its upstream Linux/macOS build or a C++11 compiler, zlib,
`-fshort-wchar` and its C SHA-1 source. It is only a resource build tool. Its
pinned Git source archive and license are in the corresponding-source bundle;
normal app builds use the audited embedded resource and VitaSDK import generator.

## Current evidence

- Native PAF adapter: 6,800 assertions under ASan/UBSan with mocked firmware.
  Tests cover loader failure, foreign singleton, unresolved constructor,
  allocation/plugin/widget failures, real scene/target/sync boundaries,
  stable typed slots, copied labels, text/style reuse, viewport/modal/media
  clipping, opacity, focus, bounded overflow, theme switching and UTF-16.
- Existing host suites: all eight theme layouts; 64 Home/search/reference
  compositions; utility coverage includes 224 QR states in IT/EN and reduced
  motion, full unobscured camera crop, editor and logs.
- UI regression includes both Legacy-family Home layouts, L1 list/fixed-deck
  navigation, all provider flows, audio layouts, settings/guide/actions,
  downloads, picker and independent preferences. Host PNGs are app-renderer
  references and are not screenshots of Sony widgets.
- Native Release/LTO, standard identity, VPK/SELF, import metadata, resource
  reproduction and source/license checks accompany the standard bundle.

## Physical Vita acceptance still required

1. Select Legacy Vita in Settings → Interface → Theme. Confirm the first frame
   and relaunch work, then exercise L1+R1 recovery at launch. Check
   `ux0:data/VitaMediaDeck/session_standard.txt` for `native PAF: ready` or its
   explicit failure code; a fallback is not a successful native test.
2. Open Home, search, folders/list and grid modes, all providers, settings,
   guide, About, logs and folder picker. Verify actual native fonts, button
   decorations, clipping, focus, touch targets and long/Unicode text.
3. Reverse L1/R1 motion quickly; open their subpages, scroll/drag through header
   boundaries and change themes repeatedly. Check stale labels and overlays.
4. Open QR with live camera; test recognition, cancellation and unavailable
   camera. Exercise normal and password IME with text editing and caret.
5. Play HW and SW video and audio; seek repeatedly, change audio/subtitles,
   show/hide HUD, minimize, reopen menus and download simultaneously. Confirm
   subtitle font/outline preferences and no extra opaque frame over video.
6. Test photos with rotation/zoom/pan and overlays, late thumbnails, empty
   states, success/error messages, suspend/resume and app exit.
7. Compare RAM/CDRAM, frame pacing and battery with Legacy on the same workload,
   including after leaving the native theme. No efficiency gain is inferred
   from this build or from the previous decoder validation.

Delivered only in the existing standard 1.4.1 VPK (APP_VER 01.05,
TITLE_ID VMDK00001, standard bubble/build ID); no dedicated test VPK, console
installation or public release is implied.
