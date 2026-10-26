# VMD button space, full-screen backdrop and continuous focus — 26 October 2026

The user reports gray native buttons, clipped/missing captions (especially the
Home Continue button), obscured lower Home shortcuts and glitchy focus after
standard revision e732fbe. That delivered package has SHA256
3249a0702b17cf828801caec6b3788ea3deeecffab8c0ae322d313854f0b7f2f.
These are physical observations; a new installed-executable/log comparison is
only claimed if recorded in this revision's console-evidence.json. Earlier
first-frame success does not establish acceptance of these visual changes.

## Buttons and their real native label

The native Button still owns its text. This update obtains its actual TextObj
through Widget::GetDrawObj(1) and sets BOTH logical and visible label windows,
plus the visible origin. Horizontal padding is eight pixels per side; vertical
padding is zero. A 160×28 control has a 144×28 text window and a 640×44 control
has 624×44. The former 4+4 vertical margin left only 20 pixels for a compact
18-point label; Home's Continue control is also enlarged from 28 to 36 pixels.
Condensation, wrapping and a second native ellipsis remain disabled. Changes
run at creation, changed dimensions or changed label, not every movement/fade.

Standalone ui::Text layout setters are never called on a Button. The new typed
TextObj setters use the already audited retail layout IDs 6/7/8 and 11/13/15,
with aligned/padded Vector2 arguments. Missing draw objects or setter failures
retain named diagnostics and Legacy recovery. All calls remain on the PAF owner.

Buttons explicitly use a sapphire-blue native background and pale text. Panel
surfaces retain the proven common Sony style with the caller's blue tint.
Button::SetStyleAnimator(STYLE_MANUAL) disables default automatic press-color
animation; navigation focus no longer sets ST_BTN_PRESS. Panels assign no caption,
so no unnecessary SetString is performed on their empty native label. Sony button textures
remain symbolic runtime references; no extracted artwork/firmware is shipped.

Four typed imports are added: Widget::GetDrawObj, Button::SetStyleAnimator, and
TextObj::SetLayoutAttribute's Vector2/bool overloads. There are now **37 imports
in six weak libraries**, including ScePafGraphics. Public pinned headers and
EMDs plus the private retail export table match all 37 pairs. An independent
ARM C++ compile checks all four new signatures. Static Button::UpdateObjsSize
inspection distinguishes its logical text-size updates from our explicit
visible-window configuration; no firmware execution, emulator or ARM JIT is used.

References read locally at the existing pinned revisions:
[ScePaf-RE Button](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf/widget/w_button.h),
[TextObj](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf/graphics/drawobj/text_obj.h),
and [NetStream original symbolic button styles](https://github.com/GrapheneCt/NetStream/tree/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/RES_RCO).

## Focus composition and smooth motion

The preceding adapter switched from a moving GPU outline to native pressed state
when rectangles came within a tolerance. That changed presentation abruptly.
Worse, focus rectangles went through ordinary opaque-content occlusion: a thin
edge intersecting a button/text could hide that entire control. Glow edges could
also modify captured alpha. A balanced decoration scope now excludes focus and
active-tab lines from occlusion, while actual modals/media retain their normal
occlusion immediately after that scope ends.

VMD uses one continuous cyan focus outline through the whole movement and settle.
A critically damped analytic spring preserves velocity on rapid retargets and
handles uneven frame times. It adds four velocity floats per focus state, with
no allocation/worker. Legacy and the other theme motion profiles are preserved.
The native vertical L1 and audio-layout selector now draw the same smoothly
tracked focus; selected settings tabs retain a thin active underline.

## Full-screen backdrop and Home

The dark sapphire gradient, inclined symbols and thin cyan waves now cover
**960×544**, including behind the status bar. VMD status chrome is translucent;
text, icons and panels remain overlays. Background cost stays one batch of
4,050 vertices / 65,536 bytes of bounded transient pool space. Reduce Motion
still freezes it. This is an allocation bound, not a measured power/frame-time
result.

Only VMD's two lower Sources/History shortcut cards are removed. Those
collections remain reachable through existing navigation. The primary media,
saved connections, favourites and recent activity use the recovered body space,
with a wider primary column, larger upper component and safe footer gap. Empty
collections still reclaim space. Invisible remembered shortcuts cannot retain
focus; both controller and touch use the resulting visible rectangles. Other
theme dashboard structures stay unchanged.

## Verification and device acceptance

Before creating the standard VPK, the production adapter passes ASan/UBSan and
TSan: **20,136 assertions each**. Regressions cover compact/wide label windows,
40 translating/selecting frames without label-layout/string churn, true missing
label/setter failure, and decorative edges retaining captions while actual
opaque content still hides them. The preceding source fails the window contract
with normal exit 1. The focus suite checks 30/60/120 Hz, rapid reverse retargets,
uneven frame times, Reduce Motion and resume. Failed host expectations were
corrected before these successful runs; no host result proves Sony pixels.

Current UI suites also pass: Home **1,012,845 assertions / 72 layouts**, native
routing/background **534,376**, chrome **30,860**, utility **37,972,502 / 224 QR
states**, settings/guide/actions **209,361**, audio **4,015**. Assertion counts
include pixel loops, not independent physical scenarios. Seventeen production
files compile with actual ARM Release/LTO flags and Werror. The 2,400-byte RCO
is reproducible; SHA256 32c0303b318e3e637ef51aac7979012748bf1b5085a8be94520b08d265cdd331.
Final ELF/import/LTO/SELF/SFO/license/source-archive checks follow the build.

Standard **1.4.1 / APP_VER 01.05 / VMDK00001**, default VMD, the dark candidate-13
icon, decoder/clock/input ownership and persisted Legacy recovery are retained.
No alternate test VPK is produced. Copy/read-back is not installation.

Physical checks for this new build:

1. Cold-start in VMD, confirm blue buttons and readable Continue/Open captions.
2. Check short and long labels in L1, settings, dialogs, QR and player panels:
   the text should use the available width with small side margins.
3. Move rapidly and reverse through Home, L1, grids and settings. Check continuous
   focus and no caption disappearances, both during scrolling and at rest.
4. Check the full-screen background under the status chrome, Home's four media
   components, empty states, footer and mini-player on/off.
5. Check audio/video native transport, HUD fades, subtitles/seek, L1/R1 and resume;
   compare Reduce Motion and another theme. Preserve logs/dump on native error.
