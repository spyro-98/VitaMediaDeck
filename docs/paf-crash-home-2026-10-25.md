# Console PAF crash and content-first Home — 25 October 2026

## Actual console evidence

The user reproduced a crash immediately after selecting Legacy Vita. The dump is
`ux0:data/psp2core-1792943617-0x00008c20c9-eboot.bin.psp2dmp`, modified at
16:53:42 Europe/Rome, 353,792 bytes, a gzip-compressed ARM ELF core. The original
is preserved on the console; a private local copy and the matching unstripped
executable are retained for diagnosis. No console dump, memory pages, credentials
or firmware binaries are committed or included in the release bundle.

Installed `app/VMDK00001/eboot.bin` SHA-256:
`fac34351726d7a8bf41cd5edf5561961cec9489d2cb67f554870a164b1a6a486`.
This exactly matches the previous standard PAF VPK's `eboot.bin`; this is not an
old app installation. Its VPK SHA-256 was
`768a03302be299b5c0ed020f6f571fb2691e63bc6323f9782792141ea6738fee`.

The main thread stopped with `0x30003` (prefetch abort). PC `0xe0252572` resolves
to `ScePaf` code segment + `0x4f932`. The stack contains application return
address `0x81084a01`; the matching ELF disassembly places the call immediately
before it at `vmd_paf_fw_create` in `ui_native_vita_prepare`. Stack candidates
are supporting evidence, not a fully unwound call chain. Widgets, plugin loading
and the first frame had not yet been reached.

The kernel output immediately before the dump says:

```text
Cannot allocate physical memory from [ScePhyMemPartGameCDialog] 0x000c6000
    REQUEST=0x00100000
```

The budget note reports a 9 MiB dialog partition with `0xc6000` bytes (792 KiB)
free. The memory-block note shows `ScePafGlobalHeap` occupying 8 MiB in dialog
memory (`0x0e20d060`). The app loaded that heap with `cdlg_mode = 1`, leaving
insufficient space for Framework's subsequent 1 MiB request. These observations
identify a concrete initialization memory error. They do not establish the
compatibility of later PAF initialization/rendering stages.

The normal `session_standard.txt` was dated 23 October and did not describe this
run. Logging is buffered and the crash happened before its next disk save.

## Correction and diagnostic boundaries

The module's global heap now uses application memory (`cdlg_mode = 0`), matching
the [pinned public standalone PAF sample](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/blob/9a68b07aa2d7247a756765bf154f292cda20aed3/paf_samples/vitasdk/main.cpp). Framebuffer integration remains Framework's
OneFrame / CommonDialog mode; the two settings serve separate purposes.
Graphics limits are explicit: 256 KiB display list, 1 MiB LPDDR parameter buffer,
1 MiB main graphics heap, 4 MiB video graphics heap, 2 MiB surface pool and 1 MiB
text pool. Pool sizes are not assumed to be additional independent OS allocations.
The general module heap remains 8 MiB. Retail allocation/fragmentation and later
firmware behavior still need a console run.

Rare initialization checkpoints save the session at module load, Framework
creation, common-resource load, plugin load, page open, ready and returned errors.
They honor the existing disk diagnostics preference. There is no per-frame or
per-playback filesystem work. Hold L1+R1 during startup for the existing Legacy
recovery path if a native theme still prevents opening settings.

`python3 tools/analyze-vita-crash.py /path/to/file.psp2dmp` reads raw/gzip ELF32
notes and reports thread PCs/module offsets and PAF memory allocations. It does
not print arbitrary application memory, kernel TTY contents or credentials.

## Home changes

Home has a full-image primary media surface with playback action and resume
progress, compact saved-server rows, a cover shelf for favourites, and activity
rows. Empty collections yield space to the primary surface where theme geometry
allows; a completely empty library has one actionable introduction rather than
four oversized empty boxes. Its primary action opens local files directly;
Triangle retains global search and sources/history have their own controls.

A real data-path defect also caused empty Home content: blank-query local scans
published only files with playback history. They now publish up to 24 unplayed
indexed files while continuing to import history from the rest of the scan.
No playback state is fabricated. A small fair provider sample comes from the
already published worker snapshot using one bounded scan, without provider calls,
allocations, sorting or filesystem I/O on the draw path. Actual resume remains
first priority; a library selection fills the primary area when there is no
resume state. Thumbnails retain the existing cache/worker APIs. Each visible
saved-server/activity row and favourite preview has its own matching touch target.
Controller cycling and identity-preserving background refresh remain available.

## Verification and remaining console acceptance

- Native PAF adapter: 6,887 host assertions under ASan/UBSan. Tests check the
  corrected module heap mode, bounded graphics parameters, rare initialization
  checkpoints and no checkpoint writes during repeated normal frames. These
  mocks do not execute Sony's constructor or graphics backend.
- Library/search: actual worker pool, indexed unplayed media, bounded fair Home
  snapshots and local state; ASan/UBSan and TSan.
- Core-note reader: four synthetic tests for raw/gzip notes, truncated input,
  invalid headers and wrong architecture; original dump parsed separately.
- Home: all eight theme geometries and availability combinations, mini-player
  space, direct server/favourite touch targets, cached no-history media and
  progressive focus; host PNGs use synthetic media and a substituted renderer.
- Native standard Release/LTO build and release/archive audits are recorded in
  the delivered bundle. Decoder implementations/ABIs and clock settings are unchanged.

On the Vita, re-test selecting Legacy Vita, restarting with it selected, native
text/buttons/loading on Home, providers, settings and QR, then video/audio with
seek and return. Inspect the new checkpoint stage if another crash occurs. Check
Home with an existing local index but no resume queue, multiple saved servers and
a mini-player; verify covers, selection and actual playback. Build/host success
alone is not confirmation that this new PAF build no longer crashes on hardware.
