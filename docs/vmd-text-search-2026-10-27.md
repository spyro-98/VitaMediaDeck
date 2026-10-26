# Native text space, search and one Home — 27 October 2026

Six physical photos show early hard clipping in headers, episode rows, audio
metadata, mini-player timing and tab captions, plus the obsolete category grid
reachable under Home. The mounted executable matches preceding standard f9bd761
(VPK SHA256 b974141baa588441fac03dacba75c2baef7bc8bc7499e9c1363d63bf9aebc0be).
Its log acknowledges native rendering. Raw photos/logs stay private; current
release evidence contains hashes. This does not prove the new build's appearance.

## Remaining text-clipping class

The preceding correction fixed a Button's own internal TextObj, but many captions
are still standalone native Text widgets, including tab labels drawn on native
panels. ui_font_draw_text used the measured Inter/PGF advance as their native
logical/visible width. Sony's font has different metrics: a short field could
hard-clip even though its enclosing row had hundreds of unused pixels.

The dispatch now separates the returned positioning advance from the rendering
window. The native window uses available screen/clip width, bounded by the latest
enclosing native panel. Its existing two-pixel glyph guard stays inside that
limit, so explicit clipped menus do not hide the whole label. ui_text_line keeps
its caller-provided width. These are geometry bounds, not invented Sony font
measurements; exact native centering/font metrics still need visual acceptance.
Podcast episode titles/details have explicit columns; date/duration use the
reserved metadata column rather than a right edge based on the other font.

A focused test compiles the **exact dispatch function extracted from font.c**
with substituted font/native boundaries, across available spans 32–924 pixels.
It checks window width and unchanged return/fallback contracts. The preceding
function fails with normal exit 1; the corrected function passes ASan/UBSan.
This isolated test does not compile the whole font module: the independent ARM
Release/Werror check does. Adapter tests separately check screen, enclosing
panel and clip bounds, including a label exactly ending at the clip's right edge.
No PAF imports, resource ABI, owner/mailbox or renderer lifecycle changes are made.

## Search results and input

Fresh searches give focus to their results. Previously the initial focus could
remain in the tabs, while VMD's legacy-backed rows supplied no persistent visible
row focus. VMD now uses native rounded row panels, cached 48-pixel artwork,
separate title/metadata lines and a smoothly moving cyan selection outline.
Rows are 64 pixels high with eight-pixel gaps: four fit above the mini-player,
five in the full content area. Drawing, touch hit tests, paging and focus use the
same rectangles. Provider/progress/cache behavior is retained. The search footer
now describes D-pad/X/Square actions rather than an unrelated options shortcut.

Actual controller scripts open a fresh search, move Down and press X in all
eight themes. Additional Legacy/VMD scripts enter from tab focus with a previous
nonzero cursor and must open the **first** result. Existing progressive-arrival,
expiry and playback-return checks retain the chosen media identity.

The tabs-to-content gesture is consumed once and callers select their first
item. It does not also move the content cursor. Radio/podcast's legacy path
seeds and retains the repeat delay before consuming Down, preventing a held
button from becoming a second fresh direction on the next frame. Shared controls
check this entry and the immediately following held frame in all eight themes.
Other repeat, wheel/grid and theme-specific navigation remains intact.

## Home, controls and background

Startup and an explicit return to Home reset the library view to the integrated
dashboard. Choosing Home while focused on its tab also opens that dashboard.
The main browser no longer calls the old indexed category-summary screen:
Video, Music and Images open the filtered file browser directly. Back from its
root returns to the dashboard. Offline podcasts use the audio file filter;
Navidrome/Immich and other providers remain available through L1. Playback
return inside the active library loop still retains navigation.

Audio, video and mini-player seek controls use actual left/right arrow glyphs
with stems (U+2190/U+2192), replacing literal less-than/greater-than signs.
Existing touch areas and playback actions are unchanged. The PlayStation-symbol
pattern changes from RGBA(48,152,204,18) to RGBA(68,178,224,34), slightly brighter
while retaining the dark gradient and thin cyan lines. Background geometry stays
4,050 vertices in one batch; no new texture, shader, network request or worker.

## Validation and remaining physical checks

The current bundle records ASan/UBSan and TSan adapter checks, the dispatch
positive/negative control, tab-order/input/persistence, online navigation,
all-eight-theme library, chrome, settings/audio and 224 QR/editor/log states.
Twenty-one production files compile with actual ARM Release/LTO flags and Werror.
Final ELF/import/LTO/SELF/SFO/license/source-archive audits run after the build.
Host pixel assertion totals are not independent device tests. No emulator,
firmware execution or ARM JIT is used.

Standard **1.4.1 / APP_VER 01.05 / VMDK00001**, default VMD and the darker icon
are preserved. Copy/read-back is delivery, not installation. On the new build:

1. Recheck the six photographed screens: long titles, episode date/duration,
   mini-player timing, tab captions and footer legends should use their space.
2. Search for a known local video and a podcast. Down/X and touch must open the
   intended row. Enter from tabs with a brief and held Down: first item first,
   then ordinary delayed repeat. Check scroll, progressive arrivals and return.
3. Startup, L1 Home, Home-tab confirmation and Back from local-category roots
   must show the integrated dashboard, never the old category-summary grid.
4. Check arrow icons in video, audio and mini-player; verify seek/pause unchanged.
   Judge the brighter symbols on the Vita display, including Reduce Motion.
