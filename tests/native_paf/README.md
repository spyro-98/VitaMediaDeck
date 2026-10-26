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
