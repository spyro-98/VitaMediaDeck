# Spatial D-pad navigation

Run `bash tests/navigation/run-host-tests.sh` with Clang. It compiles the
production grid, rectangle-focus and IPTV layout helpers under ASan/UBSan.

Checks exhaustively cover grids with 1–96 items and 1–8 columns, reachability
from every cell, visual row boundaries, nearest available item in a partial
last row, and hidden/disabled controls. IPTV forms and collections exercise
header, contextual actions, cards and scroll neighbours with 0–33 items.
The real IPTV-loop companion is `tests/iptv/run-ui-tests.sh`.

Pure geometry and host rendering do not establish physical Vita input or PAF
pixels. Folder-picker footer checks live in `tests/ui_crossfade/run-motion.sh`.
