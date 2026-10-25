# Second PAF console crash: widget factory and failed-frame recovery

## Evidence from the installed corrected build

The user reproduced another crash. After USB was re-enabled, the installed
`app/VMDK00001/eboot.bin` SHA-256 was
`0a6fa22703f7f2970163ea4bd8ba1f5be6e1607375b4ace385de756f173c1f9c`,
matching standard VPK SHA-256
`fc5804af9baf38c18c4129f8c996bb9893dae4f33457c4ed8ce1eccdba39565f`
from commit `048403aa240e75289c6a0f3dde2d414be4c89a40`.

The new persisted log is dated 25 October, 20:25:29 Europe/Rome. Its PAF sequence
reaches module, Framework, common resources, plugin, page and ready, then:

```text
native PAF: ready; Sony widgets; global heap=8388608 surface=2097152 text=1048576
native PAF: unavailable 0xFFFFAFB8
native PAF: init fallback
```

This confirms that the previous memory correction passed initialization in this
run. It does not prove that the new native presentation worked. The app's error
`-0x5048` means `present()` failed while preparing native widgets. The previous
code then hid the page, marked PAF failed, but set `changed = 1` and still called
`Framework::Update`. This contradicted its supposed fallback and is a concrete
unsafe control-flow defect.

The new dump is
`ux0:data/psp2core-1792956329-0x0008342155-eboot.bin.psp2dmp.tmp`, 158,016 bytes,
SHA-256 `17a5494dabaecc9a6b1e628b8e39d55cc644cf29a9c63e33f4451fc2bd169a4c`.
Its gzip stream decompresses, but the ELF is incomplete: 33 declared segments
are absent. Available thread/module notes show main-thread stop `0x30003`,
PC `0xe0204018` = ScePaf code + `0x13d8`, LR `0xe0292f2f` = ScePaf code +
`0x902ef`. The main thread's stack and later memory/kernel notes are missing.
No complete backtrace, new memory-budget conclusion or exact firmware-internal
crashing function can therefore be claimed. The original dump and session log
are preserved; private local copies and matching symbols support diagnosis.

## Changes

1. `Plugin::CreateWidget` now receives lowercase resource factory keys `text`,
   `button` and `plane`. The old code incorrectly supplied C++ class names
   `Text`, `Button`, `Plane`. This distinction is visible in the
   [original NetStream source](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source/debug.cpp#L151),
   which creates a native text widget with `"text"`, and in PAF's resource tags.
   Busy indicators remain pre-created resource nodes.
2. A failed presentation returns immediately. There is no further native Hide,
   flush or Update of that incomplete widget graph. Subsequent vita2d frames
   render actual Legacy. The app also persists Legacy, avoiding an automatic
   native retry on next launch. A preference-write failure is logged separately;
   current-process recovery still works. Re-selecting Native after an error in
   the same process restores Legacy without re-entering firmware.
3. Diagnostics identify failing operation, typed slot and return status, without
   logging labels, provider URLs or credentials. First-frame render/complete
   checkpoints distinguish a widget setup failure from a later firmware render
   failure. These are one-time writes, honor disk diagnostics, and add no I/O to
   repeated normal frames or media decoding.
4. The dump reader has explicit `--allow-partial` support. Strict mode continues
   to reject incomplete cores. Partial results are labeled with missing segment
   counts and never invent memory-budget data or stack frames.

No changes to Home, decoder implementations, ABI, clocks, firmware imports or
resource bytes are part of this follow-up. Native PAF remains implemented across
the shared interface; recovery does not replace it with a Sony-look imitation.
No Sony implementation, firmware binaries or extracted assets are distributed.
The downloaded NetStream source is consulted privately for API facts only.

## Verification and acceptance

The actual adapter passes 7,129 host assertions under ASan/UBSan with strict
warnings. Its factory mock now rejects unknown/capitalized keys instead of
accepting arbitrary strings. Failure injection covers widget creation, geometry,
text assignment, invalid frame targets and preference-write failure. Creation,
geometry and text failure require zero native updates, actual Legacy recovery,
no retry on subsequent frames, and safe re-selection in the failed process.
Repeated successful frames require no additional diagnostic checkpoint writes.

The core reader passes five synthetic raw/gzip, malformed/header/architecture and
partial-opt-in tests; the new real partial dump is analyzed separately. Native
Release/LTO, the unchanged 30 weak PAF imports, archive/SELF/SFO/provenance and
source/license checks accompany the standard 1.4.1 delivery.

These tests do not run Sony firmware. The corrected factory keys and failed-frame
control flow still need a new physical test. The log does not identify which
individual widget operation failed in the previous binary; the new detailed
messages address that evidence gap. The successful-frame framebuffer contract
is not newly proven by this run. Do not mark full PAF rendering accepted merely
because initialization or mocked calls pass.

Install the updated standard VPK, select Legacy Vita and verify that native text,
buttons and loading actually render. Open/close menus, then settings/QR and media
players. A detected setup error should return to Legacy and preserve app use;
that recovery is a failure report, not successful acceptance of the PAF theme.
If another hard crash occurs, keep its dump and `session_standard.txt`; the last
first-frame stage will distinguish preparation from firmware rendering. L1+R1
at startup remains the manual recovery path.
