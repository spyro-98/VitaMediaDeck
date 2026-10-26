# Legacy Vita: button-label object — 26 October 2026

## New console result

The user still sees automatic recovery to Legacy. The newly mounted console log
is dated 26 October 12:20. The installed eboot SHA-256 is
`b0505e0a07fa65e19c88ad2048bc550f83fc64da4cbf5b143806b6c2e6346369`,
matching standard VPK SHA-256
`5398f77daeacfb5315cb6fb0e6ba13700a6ba397d8c607ae6b65e09e82c072f2`
from commit `473aea8cedb11d6ffbf84e5abf62443a14f4d1c9`.

PAF again reaches ready. The precise new failure is:

```text
native PAF: widget failure kind=1 slot=1 operation=string rc=0x80AF4101
native PAF: unavailable 0xFFFFAFB8
native PAF: init fallback to Legacy
```

Kind 1 identifies a Button. Unlike the preceding log, this run reports failure
at its string assignment, rather than a Text's vector layout. A different operation
is now the first recorded failure. This does not prove all text rendering or a
complete native frame succeeded. No newer console crash dump was present.

## Cause and correction

The adapter passed draw-object index 0 to both Text::SetString and
ButtonBase::SetString. A Text owns its text object at index 0. A Button owns a
background PlaneObj at index 0 and its label TextObj at index 1. The firmware's
ButtonBase::SetString checks the selected object's type and returns 0x80AF4101
when the target is absent or is not text, matching the console log.

The production adapter now uses named class-specific object IDs: Text=0,
Button label=1. Empty strings still clear a reused button label; assignment is
not skipped or ignored to bypass the error. All draw-object-index arguments in
the adapter were inspected: these are its only string-target call sites. Button
state IDs, layout attributes, weak imports and native resources remain unchanged.
The existing failure guard still prevents native updates after a rejected frame.

The earlier host mock accepted object 0 for both classes. It now tracks each
created widget's type and rejects incorrect object indices with the exact
console error. Regression cases cover Text and Button independently, the old
wrong-index requests, changed labels, clearing/restoring labels on reused
widgets, and retained cache/geometry/recovery behavior.

## Verification

- 9,121 production-adapter ASan/UBSan host assertions passed.
- The incorrect index is rejected explicitly by the typed mock; no deliberate
  host crash is used as a regression check.
- Source inspection uses the pinned public Button/Text declarations and the
  private firmware bytes already collected in the prior session. No new Vita3K,
  Swift capture or ARM-JIT test is launched for this follow-up.
- Native Release/LTO, exact weak-import audit, source/provenance/license checks,
  documentation links and release archive checks are recorded with the package.
- Standard app 1.4.1 / Vita APP_VER 01.05 is retained. Decoder implementations,
  clock profile and media GPU path are unchanged. No Sony code/assets are added.

Full Sony UI rendering remains unverified. On console, install the updated
standard VPK, restart the app and select Legacy Vita again. Confirm it remains
active, then check text/button labels, lists/L1, settings/QR and player overlays.
If it recovers again, the current session log is needed; identical visible
recovery can be triggered by a different operation.

## References

- [Pinned public Button object declaration](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf/widget/w_button.h)
- [Earlier text-layout correction and emulator limitations](paf-layout-2026-10-26.md)
