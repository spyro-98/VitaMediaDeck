# Controller icons and spacing — 28 October 2026

VitaMediaDeck replaces face-button ASCII, Unicode shapes and spelled-out button
names with downloaded coloured PlayStation prompts. L1/R1 use matching shoulder
caps. Shared font/action paths cover Home, categories, search, network and
personal-media providers, radio/podcasts, IPTV, players, image controls,
downloads, QR, settings, guide, About, logs and modal actions.

The six assets are unchanged Kenney Input Prompts 1.5A PNGs, licensed CC0-1.0,
with SVG sources, upstream notice and download hashes in `assets/input-prompts`.
PNGs, license notice and SPDX entry ship in the VPK. Session textures load
outside drawing; icons retain their colours and follow text alpha.

Explicit private-use tokens mark app-owned copy. Ordinary X/O or L1/R1 in media
metadata, filenames and typed text are not rewritten. Measurement, fitting,
wrapping and centering include the icon slots, with six pixels of horizontal
gutter. Missing assets log a warning and retain readable fallback key labels.

VMD/PAF mixed hint lines use the shared font/sprite renderer and reserve their
region against native fills. Other labels/buttons keep the native path. L1
headers and icon-only controls are centered without duplicate captions.

## Spacing across all eight themes

The clock ends **22px before the battery** and uses the same face for measuring
and drawing even in PAF. Its 80px maximum accommodates 12-hour IBM Plex Mono
text. Wi-Fi retains an independent gap. The two activity indicators have bounded
smaller rings and separated centers; both fit beyond the centered search field
with at least 12px of clearance.

Action labels reserve 12px after the key cap and at the right edge. Footer
columns have deeper panel insets and a 20px minimum central gap. Inline icons
have three pixels of gutter on each side. Three player hints were shortened to
fit their existing panels in Deck 90's monospaced face.

Checks cover Legacy, Ghost Mesh, Deck 90, Signal, Aperture, Relay, Mosaic and
VMD, Italian/English copy, full/compact contexts and partially visible controls.
Older scene fixtures now account for VMD's Legacy geometry and its already
present 4,050-vertex backdrop. Radio discovery's ninth cover-row option is also
represented. These fixture corrections do not change application navigation.

## Validation and build boundary

See `tests/input_prompts/README.md` and `docs/VALIDATION.md`. Checks include
production font/sprites under ASan/UBSan, every clock value in both formats and
actual theme faces, shared component insets, theme scenes/navigation/motion,
native dispatch/adapter contracts, Release/LTO compilation, linked PAF imports,
packaged asset hashes and license audit. PNGs use substituted host rendering.

The relocated SDK lacked `cc1`, `cc1plus` and `lto1`. Matching executables from
official VitaSDK `master-osx-v2.540` were recovered into
`/private/tmp/vmd-sdk-recovery`, without editing the installed SDK. Temporary
SDK/workspace symlinks without spaces avoid upstream LTO/VPK quoting failures.
CMake's C and ASM flags select the recovered compiler directory. The standard
identity build is `build/pad-icons-final-20261028`; delivery uses a dated folder
under `build/releases/1.4.1`, preserving the preceding standard package.

This build has not been installed or visually/performance tested on a physical
Vita. Source changes remain uncommitted.
