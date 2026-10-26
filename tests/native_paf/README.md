# Native PAF presentation tests

Run `bash tests/native_paf/run-host-tests.sh` with clang on macOS/Linux.
The test includes the actual production adapter and replaces only firmware,
GXM/vita2d frame boundaries and log/language/theme services. ASan/UBSan check
bounds and lifetimes; strict warnings are errors. No Sony SDK, asset or firmware
is required or distributed. This does not draw Sony pixels on the host.

Checks cover lazy module initialization before a GXM scene, fixed typed pools,
foreign-framework refusal, failed/weak exports, parameters, copied labels,
text/geometry/style caching, UTF-16, clipping, scrim opacity, later modal/media
occlusion, cached paint order, source-independent subtitle text, focus, bounded
overflow, theme switching, idle update suppression and failure fallback.
Existing eight-theme UI suites separately check actual app geometry/input with
host rendering, including QR/editor/logs. Physical Vita acceptance is listed in
[the PAF report](../../docs/legacy-vita-paf-2026-10-25.md).

The regression from the 25 October console dump requires the module heap in
application memory (not the 9 MiB dialog partition), explicit graphics limits and
initialization-only disk checkpoints. Firmware allocation/rendering needs a Vita.

Run `PYTHONDONTWRITEBYTECODE=1 python3 tests/native_paf/test_core_reader.py` for
synthetic raw/gzip/core-validation tests of the read-only dump helper.

The second console regression checks case-sensitive lowercase factory keys, zero
native updates after a presentation failure, persisted Legacy recovery, repeated
frame/re-selection safety, and first-frame-only checkpoint writes. See the
[second PAF crash report](../../docs/paf-widget-recovery-2026-10-25.md). The dump
reader requires explicit `--allow-partial` for unfinished cores and labels missing
segments rather than interpreting absent memory as zero.

The text-layout regression from 26 October checks retail vector IDs 6/7/8,
boolean IDs 11/13/15, and 16-byte aligned/padded Vector2 arguments. Injected
failures at each of those six stages must preserve the exact operation and
firmware error code, recover to Legacy and never update a failed native graph.
The [layout report](../../docs/paf-layout-2026-10-26.md) distinguishes the host
contract tests, private firmware setter probes and incomplete emulator runs from
physical UI acceptance.

The subsequent console error 0x80AF4101 requires class-specific string targets:
Text object 0 and Button label object 1. The mock tracks created widget classes
and rejects a wrong target with that error. Regressions cover both old invalid
targets, updates, clearing/restoring a cached button label, and no widget rebuild.
See the [button-label report](../../docs/paf-button-label-2026-10-26.md).

The first-frame regression requires PAF's OnUpdateData (32 bytes on ARM), not a
cast of CommonDialog's 96-byte record. The firmware mock consumes the independent
PAF type and checks every field against the actual CommonDialog source, with
alternating buffers/sync, distinct format/type and reserved-area markers, nullable
depth, eight invalid targets, NULL input and first-frame-only descriptor logging.
ARM static assertions protect the production layout; an independent reference
compile-only probe was also run before the VPK build. See the [extended audit](../../docs/paf-frame-audit-2026-10-26.md).

The blank-layer regression separates target acceptance from native processing.
The firmware mock's Run uses a real POSIX thread and a per-frame condition
handshake; Update without Run paints zero modeled frames. Startup create/start
failure, absent/returned Run, delayed startup, timeout and later runner exit
must recover before declaring native readiness. None of these mocks paints Sony
pixels. See the [render-loop report](../../docs/paf-render-loop-2026-10-26.md).

`bash tests/native_paf/run-ui-routing-tests.sh` compiles actual L1/shared
components with native dispatch enabled. Checks cover native row buttons,
IT/EN/live slots, scrolling bounds, panel finish ownership, media base retention
and unchanged Legacy edges. This adds no golden Sony screenshot claim.

VMD layout regressions also check stable content/L1/R1 identities, repeated modal
cycles without widget/string regeneration, native clipping during animation,
button-owned captions, no second native ellipsis, and retained native tile
windows around media. The shared routing fixture checks the caller's available
text width, rounded backing corners and one 84-vertex panel-backing GPU batch. Sony pixels and
native animation quality remain physical-console checks.

The visibility-return regression supplies arbitrary positive and negative r0
values while Show/Hide actually update their target. They are not errno
statuses on the inspected retail implementation, despite the public RE int
return declaration. Cold init, page/child hide/show/reopen and true alpha-error
recovery must work for either sign. Real missing busy nodes remain errors. A
negative control against b11d5ad exits normally with status 1 at cold startup.

The latest [native polish report](../../docs/vmd-native-polish-2026-10-26.md)
adds real CheckBox state, one Button-owned caption, fixed logical text bounds,
no condensation, position-only movement, widget-alpha fades, scope-aware reuse
and changed-suffix ordering. A 60-frame moving/fading fixture requires no size,
string or text-style churn. The routing fixture checks the two-line footer and
the one-batch 4,050-vertex thin-cyan background, highlight/brightness bounds,
continuous motion and Reduce Motion. Existing eight-theme Home, chrome, utility,
settings and audio fixtures exercise the shared callers with host rendering.
