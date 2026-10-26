# VMD native controls, stable text and cyan backdrop — 26 October 2026

## Console evidence and scope

The installed `c9ae7eedd37b7a30b162d8a9c50df7596640865f` executable matches the
preceding standard VPK (`66664e9c6ed1174f563f8858fcedc788cac708dc6e50928269c3968c50c1bcc2`).
Its session starts in VMD (theme 7), initializes the single PAF owner and
acknowledges the native first frame. The user confirms native rendering, but
reports flicker, detached button captions, text deformation behind L1, custom
switches/transport controls and uncontained legends. This supersedes the pending
startup observation in the [preceding report](vmd-startup-default-2026-10-26.md).
Logs/settings remain private; release evidence contains hashes and this redacted
observation. No console settings, media or credentials were edited externally.

The update stays in standard **1.4.1 / APP_VER 01.05 / VMDK00001**, with VMD as
default, the darker candidate-13 icon and existing Legacy recovery. Decoder,
clock, input ownership and PAF owner/mailbox/GXM protocols are unchanged.

## Text lifetime and geometry

Opaque overlays previously resized native text widgets to the remaining visible
rectangle. PAF laid out/scaled the text again inside that smaller box; standalone
button captions could be assigned/moved independently of their backgrounds.

Text and labelled controls now keep their complete logical box. An opaque modal
hides an intersected label as a unit; a translucent veil changes widget alpha.
Rows partly outside an explicit viewport disappear as complete controls. This
intentional edge behavior avoids changing glyph proportions while scrolling;
panels still retain their media-visible cutouts. Natural screen-edge clipping
keeps the complete animated button box. Text condensation is disabled.

Moving a control only changes position. Size/layout setters run when dimensions
actually change, not during translation or fading. Text color no longer changes
with overlay alpha; fades use Widget::SetShowAlpha. Stable label identities and
scope-aware reuse keep content, L1 and R1 caches separate where capacity allows.
Only a changed paint-order suffix is reordered. Pools remain bounded.

Buttons use one real Sony Button with its own label, transform, padding and
original symbolic native style. The additional app-created Text caption is
removed. The RCO supplies font/padding and references Sony common button/glow
textures at runtime; no extracted artwork, proprietary SDK or firmware is shipped.
The source XML and reproducible embedded resource are pinned in DEPENDENCIES.lock.

## Actual Sony controls and contained legends

Settings booleans now use native **check_box** widgets and their common Sony
style. X/touch still routes through the app's existing settings actions. Checked
state changes only when needed. Sixteen fixed checkbox slots are available.
The one new import is StateButtonBase::SetCheck(bool,float,bool), NID
**0x56B9B54A** in ScePafWidget. Its void return/argument types, lowercase factory,
style and NID were checked against pinned primary references and the private
retail export table. There are now **33 app-local imports in five weak libraries**.
No private firmware was executed or included in the package.

Video/audio play, pause and skip controls now dispatch to real native Buttons,
with existing touch areas and opacity/availability preserved. Their older square
GPU controls remain the fallback for other themes. Audio layout choices and the
L1 badge also use native Buttons. Legends have native panel containers in L1,
audio-layout selection, settings, shared footers, Home/library and player R1.
The two-line browser footer fits both lines and stays below the last row with
and without the mini-player.

Primary references: [PAF component samples](https://github.com/Princess-of-Sleeping/vitasdk-paf-component),
[ScePaf-RE headers](https://github.com/GrapheneCt/ScePaf-RE/tree/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf),
and [NetStream native resource styles](https://github.com/GrapheneCt/NetStream/tree/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/RES_RCO).
The original single-owner and frame-ABI rules from the preceding audits remain.

## Dark background with thin cyan glow

The original GPU background now has a darker sapphire gradient, five differently
phased cyan curves and a discreet repeating inclined cross/circle/triangle/square
pattern. Each curve has a **1.3-pixel core** with two low-alpha outer glow layers;
there are no broad bright ribbons. Wave periods range from 5 to 10 seconds and
the symbol pattern moves continuously. Reduce Motion freezes the composition.

All glyph/curve geometry uses precomputed templates. Per frame it submits exactly
**4,050 vertices in one GPU batch**, using a bounded 4,096-vertex/65,536-byte
vita2d pool allocation. There is no added texture, shader, render target, heap
allocation or worker. Shared GPU MSAA remains 2x. These are resource bounds,
not measured Vita frame time, battery or native visual-quality claims.

## Validation

All checks below completed before the standard VPK build; final import/LTO,
SELF/SFO, licenses/provenance and source-archive checks follow the build. Logs and
source hashes are included in the current release validation directory.

| Check | Result and boundary |
| --- | --- |
| Production adapter, ASan/UBSan | 18,870 assertions; mock firmware boundaries |
| Production adapter, TSan | 18,870 assertions; actual threaded owner/mailbox |
| Movement/fade regression | 60 frames: 3 widget creations, 2 string assignments, 3 size calls, 2 text-style calls, 3 initial reorders; only position/alpha change thereafter |
| CheckBox state | Owner-thread setter only at creation/state change; correct widget kind and ABI-shaped arguments |
| Preceding source negative control | Normal exit 1 at detached-caption contract; no intentional crash |
| Native routing/background | L1 IT/EN/live rows, bounded text, footer containment, rounded backing, one 4,050-vertex batch, thin highlight bounds and Reduce Motion |
| Home | 1,013,459 assertions; 72 layouts, all eight themes, navigation/return/loading |
| QR/editor/logs | 37,972,502 assertions; 224 QR states, eight themes, IT/EN/Reduce Motion |
| Chrome | 30,860 assertions; eight themes, IT/EN/status/command decks |
| Settings/guide/about/actions | 209,361 assertions; eight themes, IT/EN/mini-player |
| Audio UI | 4,015 assertions; layouts, existing playback actions and eight themes |
| Independent CheckBox ABI | Public-header C++ type check compiled for ARM; no firmware execution |
| ARM Release | 15 production files with actual GCC/LTO flags and Werror |
| Resource | Recompiles byte-identically; 2,336 bytes, SHA256 2949e60991225abcb633fbd64f5b2afe0616b889771a49ff276bcb8c4af36d92 |

The first local background prototype exceeded the host rasterizer's 4,096-vertex
limit and failed its assertion. Production geometry was reduced to 4,050 before
subsequent successful tests/build. No emulator, firmware execution or ARM JIT
was used. Pixel-loop assertion counts do not represent independent test cases.
Host fixture screenshots check app geometry and the original background, not
Sony fonts/buttons/check boxes, real GXM timing or physical display contrast.

## Physical acceptance for this new build

1. Install the standard VPK from Downloads, cold-start twice and confirm VMD
   remains selected. The copy/read-back operation itself is not installation.
2. Open/close L1 and R1 repeatedly, move quickly between pages and scroll L1 in
   both directions. Captions must stay inside their buttons; underlying text
   must keep its proportions, without the previous regeneration flash.
3. Check settings in Italian/English, every settings tab, on/off controls, Home,
   grids, library/search, guides, dialogs and QR. Verify contained legends and
   the two-line browser footer with/without the mini-player.
4. Play video with HW then SW, and audio. Check native play/pause/skip, R1, fade,
   subtitles/seek, full-screen media, suspended/resumed playback and navigation.
5. Check subtle cyan glow and visible continuous motion on the physical display,
   including Reduce Motion, and judge contrast/animation under actual playback.
6. Switch to another theme and back; confirm existing input behavior. L1+R1
   recovery and later explicit Legacy selection remain available.

If any new native error occurs, preserve session_standard.txt and the console
dump. Constructor/setter/owner failures still recover; no status checks have been
removed from genuine error-returning setters in this revision.
