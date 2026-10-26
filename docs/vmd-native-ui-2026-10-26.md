# VMD: native text, tile finishes, menu reuse and Home — 26 October 2026

## Console result and scope

The preceding standard release (`cb526c8`) now reaches Home. Its installed
executable matches the release VPK, and the console log records the first native
owner snapshot being acknowledged. The user confirms native PAF presentation
is visible, but reports premature ellipses, captions outside buttons, square
blue tiles and flicker when opening menus. These observations establish progress
for that preceding binary, not acceptance of this new revision.

The theme is now named **VMD** in Settings and the English/Italian controls guide.
Its stable on-disk ID remains 7 (`VT_UI_THEME_LEGACY_VITA` internally), preserving
saved theme choices, per-theme tab order and browser preferences. Fresh profiles
use VMD by default. Valid existing preferences still win, including explicit
Legacy selections and the persisted Legacy crash recovery. L1+R1 recovery remains.
No settings, credentials or media were edited directly on the mounted console.

## Shared corrections

### Text and button captions

The explicit-width text helper previously reduced the native widget to the
fitted Inter glyph advance. The native system face then received both that
smaller window and an enabled native ellipsis policy. It could abbreviate text
that had already been fitted by the application. The helper now passes the
caller's actual available width; genuinely long strings still get the existing
single application fit. Native Text disables its second ellipsis and uses bounded
horizontal condensation. Generic text calls keep their established visible
window and clipping path.

Native buttons retain Sony's actual button widget and common nine-slice style,
but their default built-in caption is cleared. Captions use separate native
Text widgets with explicit horizontal padding, an inner height, centered native
layout and a smaller size for short controls. Geometry, clipping and fade apply
to the caption as well as its button. This shared path covers tabs, actions and
L1 rows. The common text policy also applies to status bars, the players,
settings and utility screens. Media/subtitle rendering retains its existing path.

This changes layout policy, not the PAF ABI: no new firmware imports were added.
The existing owner still performs every native widget operation. Public type,
layout and button contracts were rechecked against the pinned
[ScePaf-RE headers](https://github.com/GrapheneCt/ScePaf-RE/tree/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf)
and the symbolic common resource usage in
[NetStream](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/RES_RCO/netstream_plugin.xml).
Exact Sony glyph fitting still needs physical display verification.

### Native tile finishes and composition

Large native panels now use the Sony button factory/common style rather than
an unstyled plane. Focus can target both panel and button commands. Folder
backings also route through native decoration. Under media cutouts the app keeps
a rounded GPU backing; the native visible caption/edge window is retained when
possible instead of suppressing the entire tile. Opaque covers clip remaining
native windows; real thumbnails/video are never painted over by native fills.

The rounded backing is one GPU triangle batch: 28 bounded perimeter samples,
84 vertices / 1,344 transient vertex bytes per panel, no textures, shaders,
per-frame heap allocation, extra framebuffers or per-corner draw calls. Device
CPU/GPU timing, memory use and battery effects have not been measured here.

### Menu persistence and animation

Commands now carry stable identities. Content, L1 and R1 use separate scopes;
visible identities are reserved before selecting a replacement cache slot, so
an overlay cannot steal a slot still needed by that snapshot. Cached widgets
and strings survive closing and reopening a menu, within the existing fixed
pool limits. Root content uses stable command identities; overlay captions also
retain identity while their position changes or rows are clipped. Partially
visible native buttons stay native throughout the transition, instead of
switching between immediate and native renderers at the screen edge.

Scope restoration covers the L1 audio-layout page, fully off-screen drawers and
other early exits. It is applied to shared R1 provider menus, local files, indexed
media, network browsing and audio/video player sidebars. Native owner, mailbox,
post-scene GPU handshake and fallback semantics are unchanged. No worker or disk
write was added to the rendering path.

### Dark themed background

VMD also has an app-owned dark blue wave backdrop behind its Sony widgets.
Two low-contrast ribbons drift over 30 seconds, with peak wave opacity 18/255;
Reduce Motion freezes their phase. A bounded single GPU batch uses 1,224
vertices / 19,584 transient bytes; curves are precomputed and only two phase
trigonometric calls are needed per draw. No bitmap, new shader, thread or heap
allocation is used. The normal video path is not replaced by this background.
The host fixture verifies low channel levels, distinct shading, animation,
identical frames with Reduce Motion and the single-batch budget.

## Provisional application icon

The selected icon is candidate 13, **PAF media hub**, the darker of the two new
blue variants. Its existing 128x128 indexed opaque PNG is copied byte-for-byte
to `sce_sys/icon0.png` and the three legacy branding filenames. The existing
runtime already loads `icon0.png` once and shares it between the header,
loading screen and menus; no duplicate texture or per-frame loading is added.
The README uses the matching full-resolution master. Candidate 12 remains a
comparison, not the active app icon. Export format, source identity and the
final VPK entry are checked separately from the physical bubble display.

## Integrated Home

Legacy and VMD content starts at y=104 instead of y=146; its title baseline is
86, directly below the existing top chrome. Other themes retain their own
composition. The initial empty hero opens the unified sources browser and its
localized action has enough width. The local source opens the filesystem
browser directly. It cannot enter the former indexed Home while still showing
the unified Home tab. Circle returns through the existing parent navigation;
other media tabs keep their regular indexed views.

## Verification and limits

Before packaging: production native adapter checks under ASan/UBSan and TSan,
including repeated modal open/close cycles without new widget creation or string
rewrites, stable underlying content, partial clipping and media caption windows;
shared native routing checks for explicit text width, rounded corners and one
GPU batch; fresh VMD preferences, preserved saved Legacy/recovery and old record
migration; unified Home navigation including the empty hero and Circle in all
eight themes; chrome and utility/QR suites; actual ARM Release/Werror syntax and
all existing native-import/resource audits. Exact counts/logs are recorded in the
release validation bundle after execution.

Host screenshots use synthetic media and substituted platform/rendering APIs.
They check geometry and state, not Sony glyph rasterization, nine-slice pixels,
native animation pacing or hardware performance. No emulator/JIT was launched.
Native Release/LTO, final ELF imports, archive identity/source parity and USB
read-back are separate checks from installation and physical acceptance.

On Vita, verify the reduced top gap, sources/file-browser/Circle navigation,
short labels without premature ellipses, longer labels staying in their bounds,
file/folder grids and repeated L1/R1 open/close/scroll in provider and player
screens. Include settings, guide, keyboard and QR. Existing custom themes and
Legacy must keep their navigation/style. Check new-profile VMD default and an
explicit saved Legacy selection separately, without deleting personal data just
to exercise that test. Playback/seek/subtitles/suspend need the usual hardware
regression pass; decoder implementations and clocks did not change.
