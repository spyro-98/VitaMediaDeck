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
