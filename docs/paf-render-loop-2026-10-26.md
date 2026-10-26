# Legacy Vita: blank native layer and render-loop correction — 26 October 2026

**Superseded for thread ownership:** the subsequent console run crashed in
RMutex::Unlock. The [new ownership diagnosis and adapter-wide correction](paf-thread-ownership-2026-10-26.md)
records the failed main-init/worker-Run split below and the missing test contract.
This document retains the original implementation snapshot; it is not current
physical acceptance evidence.

## Physical evidence and scope

The user's three photographs show the app's blue base, thumbnails, immediate
icons and lines, but no native text, including status and L1 labels. The console
session at 13:46 reaches `first-frame complete` without fallback or a widget
error. The installed eboot matches the standard package from commit `803bc1e`:
VPK SHA-256 `ccd7c3cce113bc1cdf97841436f9df9382a46c297cd51192437ea3e63e29e1d2`.
The log SHA-256 is
`fe7f734137c3630879c24e3687d8a7176880955e3745aa1b07e3d05dbb64a2f8`.

This establishes a blank native layer after target setup, not successful native
painting. `first-frame complete` previously meant only that Update returned.
The earlier framebuffer-record correction remains necessary and integrated.
Private photographs, firmware code and console dumps are not packaged.

## Missing lifecycle step

Every reviewed public PAF UI example enters Framework::Run after preparing its
resources/page. Our external-loop adapter never entered Run. Private, read-only
static inspection of previously collected retail PAF confirms the distinction:
Update supplies the target and only wakes/waits for a Run loop if its active
count is nonzero. With no Run, it can return without processing/drawing widgets.
The prior mock incorrectly treated every accepted Update as a painted frame.

OneFrameMode does **not** turn Run into a one-frame function that can safely be
called on the app thread. Run remains blocking and sleeps between Update
handshakes. The adapter now starts one process-lifetime kernel thread, with a
256 KiB stack, for Run. Before accepting native commands it waits for the
ABI-defined active-run count at offset zero of Framework. This is a read-only,
acquire load of the aligned prefix; no firmware object field is written. The
wait is bounded to 2,000 one-millisecond startup delays. A failed thread create,
start, missing weak Run, startup timeout or stopped renderer restores Legacy.

The main app retains navigation, vita2d scene submission and media rendering.
It mutates the native widget cache between handshakes, after its scene ends;
Update then wakes PAF and waits for that frame's native processing. The existing
actual target/sync conversion remains. No new polling, disk logging or native
updates are added to hidden-page playback: the native runner waits when idle.
After detected failure the process-lifetime graph is no longer submitted.
The loaded framework and waiting thread are retained until process exit, as the
backend is not hot-unloaded underneath system services.

The new weak Framework::Run binding brings the total to **31 functions in four
weak PAF libraries**. All dependency versions, media paths and clock settings are
unchanged. No Sony headers, implementation or assets are shipped.

## Native finishes

Shared panels retain their base fill under later media/camera cutouts, but stop
adding the old Legacy edge decorations once a native panel is accepted. Focus
remains available, including large panels. Fully visible L1 rows use actual
firmware-styled buttons and labels, retaining existing row rectangles, groups,
scrolling, hit targets and menu translation. Edge-clipped rows retain clipped
text while moving into view. The other themes keep their existing rendering.

These changes address native composition and dispatch. They do not claim to
replicate the Sony shell or prove Sony pixels on a host renderer.

## Public examples and independent ABI review before the build

- [PAF text example](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/blob/9a68b07aa2d7247a756765bf154f292cda20aed3/paf_samples/samples/paf_sample_cxml_text/src/paf_sample.cpp)
  and [button example](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/blob/9a68b07aa2d7247a756765bf154f292cda20aed3/paf_samples/samples/paf_sample_cxml_button/src/paf_sample.cpp): resources/page then Run.
- [ScePaf-RE Framework declaration](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf/framework.h): Run signature, OneFrameMode and active-count prefix. The blocking handshake behavior was checked separately against retail instructions; declarations alone do not prove it.
- [NetStream main](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source/main.cpp)
  and [libvita2d_sys_paf](https://github.com/GrapheneCt/libvita2d_sys_paf/tree/011bc559c0e46ad9ddd6088f3a76e4f0bc57fa2b): PAF-owned rendering/graphics integration, not evidence that target-only Update paints.
- Independent ARM C++ compile-only assertions verify Framework=124 bytes,
  run count offset=0, option offset=116, OneFrameMode=1 and the exact Run type.
  Production changed C files also pass ARM syntax checks with actual Release
  options and warnings treated as errors. These checks create no VPK.

## Pre-build checks

- Actual adapter under ASan/UBSan: **10,846 checks**, using a real POSIX runner
  thread and condition-variable frame handshake. An explicit target-only case
  accepts Update but paints zero modeled frames. Tests cover single startup,
  frame completion, cache reuse, alternating targets, hidden/idle pages,
  delayed startup, timeout, weak/returned Run, create/start failure and later
  runner exit, in addition to the earlier text/layout/occlusion regressions.
- Actual shared components and L1 compiled with native dispatch enabled:
  **1,467 checks**, IT/EN, live slots, every selectable row and scroll target,
  native button dispatch, panel finish ownership and unchanged Legacy borders.
- Existing chrome: **20,052 checks** across eight themes, IT/EN, time modes and
  live activities.
- Utilities: **224 QR states**, all eight themes, IT/EN/reduced motion, intact
  synthetic camera crop, editor/logs. The 37,923,866 pixel/state checks are not
  that many independent scenarios or physical tests.
- Source/import/resource/provenance/license audits and relative Markdown links.
- No emulator, firmware execution, JIT or capture helper was started.

A timestamped pre-build gate records these results and the unchanged previous
standard VPK before building. Host fixtures and mocks do not draw Sony pixels.
The resulting native Release/LTO, actual weak-import table and VPK/source/archive
checks are recorded separately in the standard release bundle.

## Required Vita acceptance

Install the updated standard package, restart and select Legacy Vita. Confirm
that labels are visible on Home, status bar, provider/file lists and L1; check
native button finishes, group headings, scroll clipping and focus. Then check
settings, QR/editor, theme switching, media overlays, subtitles and suspend.
Retain session_standard.txt and a new dump if an error occurs. L1+R1 during
startup remains manual recovery. Successful Update/Run synchronization alone is
not final visual, media-coexistence or performance acceptance.
