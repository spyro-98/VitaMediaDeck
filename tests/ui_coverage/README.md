# Theme coverage of utility screens

Part of the 1.4.1 validation set. See [suite index and evidence boundaries](../../docs/VALIDATION.md).

Run `bash tests/ui_coverage/run-host-tests.sh` on macOS with clang, FreeType and libpng.
Set `VMD_COVERAGE_OUTPUT` to retain renderings in a chosen directory.

The fixture compiles production `qr_view.c`, `brand.c`, `log_viewer.c`, theme scenes,
panels, status indicators and focus drawing. Platform readings, file I/O and the
camera texture are substituted. ASan/UBSan are enabled. Deck 90 uses the real
IBM Plex Mono face; other themes use Inter.

Checks cover seven QR states, eight themes, Italian/English and reduced motion
(196 compositions), unchanged crop coordinates/scale, every displayed camera
pixel, cancel geometry, pairwise distinct themes, the input backdrop and log
list/content. Reduced-motion text editing must stay identical across frames.
Maximum unsigned frame counts exercise clipping without overflowing the page.

`tests/ui_crossfade/run-host-tests.sh` additionally renders every Settings tab,
About, categorized guide, plain/tabbed text readers, file actions, deletion,
download states, success/errors, player startup and seeking in all palettes.
`tests/ui_chrome/run-host-tests.sh` covers the real top bar/L1/status.
`tests/ui_crossfade/run-scenes.sh` covers browser and media scene composition.

These are host renderings of native drawing code, not Vita screenshots. They do
not establish camera acquisition/QR recognition, native IME compositing, console
frame rate, OLED appearance or network-provider authentication.
