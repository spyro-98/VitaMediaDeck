# Legacy Vita: first-frame crash and extended PAF audit — 26 October 2026

## Console evidence

The new dump is `psp2core-1793014239-0x000f2320b1-eboot.bin.psp2dmp.tmp`,
177,280 bytes, SHA-256
`71dd46058eb2fb956bf0a27604e5e796175dc04bced15ef03e14159757fcb644`.
The installed eboot SHA-256 is
`3e09531f3e36f3488742548cba4bddf4157000dba25a68986474779d365e33d6`,
matching standard VPK
`57bb99c3392e236ad7f81d5102cfb02ef7372371d5fd23e86c0d6e7a280acd39`
from commit `d8a3602d070f2d2c8f1feb16ffd604d60e826034`.

The 12:30 console log reaches PAF ready and `first-frame render`, without a
recorded widget failure, but never records `first-frame complete`. This run
therefore reaches native update after widget preparation. It does not establish
successful native pixels.

Available core notes show main-thread stop 0x30003, PC ScePaf + 0x13d8, LR
ScePaf + 0x902ef, and r0=0x805B0003 (SCE_GXM_ERROR_INVALID_VALUE). The dump is
partial: 33 declared segments, including the relevant stack, are missing.
There is no unwound backtrace or captured frame-parameter block. Previously
collected private PAF code has a breakpoint assertion at the matching PC and a
render-target-size query/assertion at the matching LR; this supports a render
setup assertion rather than proving every intermediate call on this firmware.

## Concrete ABI defect and correction

The adapter cast SceCommonDialogUpdateParam directly into Framework::Update.
The pinned PAF declaration specifies cdlg::OnUpdateData for this path: 32 bytes
on ARM, compared with 96 bytes for SceCommonDialogUpdateParam. Their field order
also differs. For example, PAF's width at byte 4 receives CommonDialog's color
buffer address; PAF's height at byte 8 receives the surface-type enum. This is a
deterministic interpretation error, independently of unavailable stack memory.

The adapter now constructs its own typed update record, copying the actual
vita2d target and sync object field by field:

| PAF field | ARM offset | CommonDialog source |
| --- | --- | --- |
| Pitch in pixels | 0 | renderTarget.strideInPixels |
| Width | 4 | renderTarget.width |
| Height | 8 | renderTarget.height |
| GXM color format | 12 | renderTarget.colorFormat |
| GXM surface type | 16 | renderTarget.surfaceType |
| Color buffer | 20 | renderTarget.colorSurfaceData |
| Depth buffer | 24 | renderTarget.depthSurfaceData, including NULL |
| Display sync object | 28 | displaySyncObject |

Compile-time ARM assertions protect the full PAF record and the incompatible
CommonDialog input layout. Missing color/sync, wrong output dimensions or
invalid stride stop before firmware update and trigger the existing Legacy
recovery. Output remains the app's fixed 960x544. The first valid frame records
its dimensions/pitch/format/type/depth availability before Update; subsequent
frames add no disk checkpoints. Buffer addresses and media data are not logged.

## Online examples reviewed before the VPK build

All source reads were pinned; no reference implementation or firmware was added
as a dependency or copied into the app.

| Primary source | Revision | What was checked / applicability |
| --- | --- | --- |
| [ScePaf-RE OnUpdateData](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf/paf_cdlg.h) | b76d6ae00516b107d37e7e8a1a16211e3b729a07 | Exact eight-field CommonDialog PAF record and native declarations |
| [PAF component loader and samples](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/tree/9a68b07aa2d7247a756765bf154f292cda20aed3/paf_samples) | 9a68b07aa2d7247a756765bf154f292cda20aed3 | Application-memory module heap; Framework/common-resource/plugin/page order; text, button and busy lifecycle. These samples use Mode_Normal/Run, not our external loop |
| [NetStream main](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source/main.cpp), [AV surfaces](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source/players/player_av.cpp) | 99db706d860f7126b0b8b6ac1f5a5bbf960055f8 | Framework-owned event loop and video surface sizing/stride/lifetime; not a drop-in recipe for two existing graphics paths |
| [libvita2d_sys_paf](https://github.com/GrapheneCt/libvita2d_sys_paf/tree/011bc559c0e46ad9ddd6088f3a76e4f0bc57fa2b) | 011bc559c0e46ad9ddd6088f3a76e4f0bc57fa2b | Draw calls inserted into PAF's graphics context/queue. This is an alternative architecture, not proof our CommonDialog bridge already worked |
| [App-pinned libvita2d](https://github.com/xerpi/libvita2d/blob/a8f15ab09d5233f0a4e4ad0e8f6ade0da888cbed/libvita2d/source/vita2d.c) | a8f15ab09d5233f0a4e4ad0e8f6ade0da888cbed | EndScene, current back buffer, real color/depth pointers, pixel stride and matching display sync before swap |
| [VitaSDK CommonDialog record](https://docs.vitasdk.org/group__SceCommonDialogUser.html), [GXM error declaration](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/gxm.h) | Local compiler/header snapshot also checked | Independent input ABI and meaning of 0x805B0003 |

The reference text-layout enum mismatch documented earlier remains explicitly
handled by the app's retail IDs. Public declarations are not assumed to prove
all retail behavior. The app uses conservatively aligned vector storage; the
previous alignment choice alone was not established as a cause of a crash.

## Extended source and protocol review

| Boundary | Evidence / result |
| --- | --- |
| Lazy module loading and allocation | Loader example and actual console readiness agree on application-memory heap; bounded graphics heaps retained |
| Opaque Framework/plugin/page/string records | Existing ARM size/offset checks and real console initialization retained; new frame checks independently added |
| Imports | All 30 named functions/NIDs remain in four weak libraries; no new SDK/runtime dependency |
| Widget factory and string targets | Lowercase resource keys; Text object 0, Button label 1; class-aware mock rejection and label reuse retained |
| Text vectors and layout | Corrected retail IDs, bounded UTF-16 and explicit vector storage retained; no reinterpretation of reference enums |
| Frame ownership | App ends vita2d GXM scene, translates the current target, calls PAF, then swaps; no framebuffer guessed from front-buffer APIs |
| Sync and depth | Actual sync object copied; nullable depth copied; alternating buffer/sync variants tested |
| Failure lifecycle | Failed widgets/targets never enter native update; fallback persistence and repeated selection still guarded |
| Idle/theme changes | Cached widgets, clipped/media cutouts, page hide and idle update suppression retained |
| Diagnostic cost | First-frame descriptor only; no per-frame playback/disk logging added |
| Native editor and QR | Editor uses sceIme rather than a second SceImeDialog; utility/chrome host suites exercise all eight themes |

## Checks completed before creating a new VPK

The pre-build gate records the unchanged previous VPK hash, timestamp and source
hashes before configuration/build starts.

- Independent C++ compile-only probe against the pinned PAF declaration:
  OnUpdateData=32 bytes, all eight field offsets; CommonDialog=96 bytes.
- Separate C compile-only ARM probe against the production header: all eight
  update-record offsets and size match. These probes produce objects, no VPK.
- Production adapter: 10,434 ASan/UBSan assertions, including every translated
  field, distinct format/type and buffer markers, alternating sync objects,
  optional depth, eight rejected target cases, NULL input, exact widget errors,
  lifecycle, caching, clipping and first-frame-only log writes.
- Chrome: 20,052 host assertions across eight themes, languages, time formats,
  live activities and command menus.
- Utilities: 224 QR states across eight themes, IT/EN and reduced motion, editor
  and logs; the 37,923,866 checks include pixel comparisons, not that many
  independent test scenarios. QR and L1 host fixtures were inspected visually.
- Native import/resource source audit and diff whitespace checks passed.
- No emulator, capture helper or ARM-JIT experiments were launched.

Host images use the host renderer and synthetic camera frames. They do not show
Sony widget rendering or physical camera operation. Release/LTO, linked import,
archive, source/provenance/license and documentation checks are recorded with
the resulting standard package. No Sony firmware/code/assets or private dumps
are included in it.

## Remaining console acceptance

Install the updated standard VPK, restart and select Legacy Vita. First check
that it stays active and that the log reaches first-frame complete. Then check
text/buttons, clipped/scrolled lists, L1/settings, QR/editor and media overlays.
Playback, seek, subtitles and suspend/resume remain physical acceptance items.
A successful compiler, mock or screenshot suite is not full native acceptance.
