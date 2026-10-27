# Restore local images beside Immich — 27 October 2026

The Images tab now always offers **Local images** beside Immich. This local
entry opens the existing device/folder browser with the image filter, regardless
of whether the indexed catalogue is empty, refreshing or already populated.
Immich stays first in its category; indexed local folders/images remain after
the permanent local entry. The shared entry path applies to all eight themes.

## Cause and correction

The category previously contained provider entries plus nonempty indexed local
roots (or indexed files in flattened mode). Zero image counts therefore left
Immich as its only visible item. The mounted console log contains scans with
22 images, then zero images, and later 29 images. The current index and root
summary both contain 29 images under `ux0:picture`; these are snapshots, not an
independent reproduction of the exact UI frame reported by the user.

A constant-size, localized presentation entry keeps direct local navigation
available even before an index is ready. It does not write a fake item into the
index, trigger remote requests, walk storage on the render thread or create a
new Home. The actual filesystem browser checks available `ux0:`/`uma0:` roots
when opened. Directory selection now uses the item's directory flag in both
button and touch dispatch, so the local shortcut also opens folders in flattened
mode instead of attempting to display an empty image path. File actions exclude
the shortcut; its description replaces a misleading zero-byte size.

The image viewer, thumbnail cache, provider routing, default theme, decoder
libraries, clocks and IPTV fixes are unchanged. Back from the viewer retains the
local image browser, then returns to Images. Only the index-independent local
entry is new; the original category tabs remain.

## Verification

- The actual entry functions pass 15,215 ASan/UBSan checks across empty and
  populated indexes, folder and flattened views, paged files/root pointers,
  provider ordering and unrelated category mappings. The preceding source fails
  the missing-local-entry assertion with normal exit 1, without crashing.
- The actual `main.c` browse loop passes 298 ASan/UBSan assertions, including
  Images → device roots with image filtering → image viewer → browser → Images.
- Existing eight-theme UI and command-deck regression, actual ARM Release/LTO
  compilation and source/native import/LTO/license audits accompany the standard
  VPK. Logs and hashes record the checks actually run; host drawing does not
  establish Sony PAF pixels or physical touch behaviour.
- The previous standard VPK's IPTV HW playback was confirmed by the user. This
  change does not alter its decoder code, but the new package's local Images UI
  still requires the user's physical-console test. No emulator, Vita firmware or
  ARM JIT was executed for this correction.

## Console check

Open Images: Immich and Local images must both be present. Open Local images,
choose the memory card and `picture` (or another folder containing images), open
a photo, and return to Images. Repeat with flattened navigation enabled and
before a scan has completed. Existing indexed photo folders and Immich remain
usable. Console storage and saved preferences are not edited by the assistant.
