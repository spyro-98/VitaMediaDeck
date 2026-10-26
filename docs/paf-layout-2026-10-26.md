# Legacy Vita: text layout fallback — 26 October 2026

## Console evidence

The mounted console runs standard VitaMediaDeck 1.4.1 / APP_VER 01.05. Its
installed eboot SHA-256 is
`a91eb2af5348f127c8af97289fac60d25a0e83fc2ce6579de3739b249b0fbf1a`,
matching VPK SHA-256
`3083d48d957c7f99624c104d5f685c85f81fc84e90fc4f3512e7f55e9021ffd8`
from commit `b4f96bd4d99a5d176ecd53c6d7a19a3d1665cd02`.

The latest session log, dated 25 October 23:54 on the mounted volume, reaches
module, Framework, common resources, plugin, page and ready, then records:

```text
native PAF: widget failure kind=0 slot=1 operation=text-size-layout rc=0xFFFFFFFF
native PAF: unavailable 0xFFFFAFB8
native PAF: init fallback to Legacy
```

This is a detected first-text layout failure, followed by the intended persisted
Legacy recovery. The displayed FFFFFFFF was the adapter's hardcoded -1, rather
than the original firmware return value. No newer console dump was present.
The earlier memory and widget-factory corrections passed these initialization
stages in this run; successful native presentation is still not established.

## Corrected contract

The adapter used vector layout IDs 5/6/7 from the pinned reverse-engineered
header. A private debugger inspection of the real firmware PAF loaded in Vita3K
showed that its vector setter accepts 6/7/8 and rejects 5. Seven bounded ARM
setter checks on private Sony 3.74 PAF code with synthetic layout objects found:

| Request | Result |
| --- | --- |
| Vector attribute 5 | 0x80AF2302, rejected |
| Vector attribute 6 | Accepted; updates logical width/height |
| Vector attribute 7 | Accepted; updates visible origin |
| Vector attribute 8 | Accepted; updates visible extent |
| Boolean attributes 11, 13, 15 | Accepted; update their respective option bits |

The app now uses named retail IDs: size 6, visible position 7, visible size 8,
condense X 11, word wrap 13 and ellipsis 15. Integer alignment IDs 0/1/2 remain
unchanged. Vector arguments now have the 16-byte size/alignment required by the
SIMD Vector2 ABI. The observed fallback points to the rejected attribute; it does
not prove the previous argument size independently caused a failure.

Layout errors preserve the actual firmware return code and identify alignment,
word wrap, ellipsis, condense, logical size, visible position or visible size.
A real failure still stops before Framework::Update and persists Legacy. No
error is ignored and the fallback has not been disabled.

## Verification and limits

- Actual production adapter: 8,318 ASan/UBSan host checks passed, including the
  corrected typed IDs and vector alignment, caching/clipping, and injected
  failures at all six changed layout stages. Each failure verifies its original
  error code, exact operation, persisted Legacy and zero native updates.
- The seven private ARM checks execute only text-layout setters against
  synthetic state. They establish the checked 3.74 setter behavior, not an
  initialized Framework, widget renderer or physical 3.65 acceptance.
- Unchanged-VPK Vita3K 0.2.1 build 4232-92bda548 tests load real PAF. With its HLE
  sysmodule loader they reach Framework initialization, but no first native
  frame completes within 90 seconds. Unimplemented kernel/GXM functions and
  host faults prevent treating the emulator as a full UI acceptance test.
- The emulator experiments also generated host crash reports for Vita3K, a
  Swift window-capture helper and the Python ARM helper (SIGILL in the sandbox).
  Those failed attempts are not passing checks. All owned test processes were
  stopped, the private bootimage restored and GDB disabled. No further emulator
  launches were made after the user's report of these crashes.
- No Sony module, extracted code, firmware asset or private console dump is
  included in the app, source archives or this report. The app's 30 weak PAF
  imports and native resource are unchanged.

The corrected standard VPK still needs a physical retest: select Legacy Vita
again, verify it stays selected, then inspect text, clipped lists, settings/L1,
QR and player overlays. If it recovers again, retain session_standard.txt; it
will now preserve the actual failing layout operation and firmware code.

## Reference facts

The pinned [PAF ABI reference](https://github.com/GrapheneCt/ScePaf-RE/tree/b76d6ae00516b107d37e7e8a1a16211e3b729a07)
provides symbol/signature facts. Its text-layout enum is not used as the retail
numeric contract after the mismatch observed here. The emulator's
[module loader](https://github.com/Vita3K/Vita3K/blob/92bda548f3aa0691102471a72725587e00676383/vita3k/modules/module_parent.cpp)
and [module selection](https://github.com/Vita3K/Vita3K/blob/92bda548f3aa0691102471a72725587e00676383/vita3k/module/src/load_module.cpp)
explain the distinct HLE/LLE test paths.
