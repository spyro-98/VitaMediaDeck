# Legacy Vita: PAF thread-ownership crash — 26 October 2026

**Subsequent result:** this build completes its first native frame but returns
to Legacy through the generic app recovery code. The [full contract audit](paf-contract-audit-2026-10-26.md)
replaces the same-frame task assumption described below and records the new
uncertainty, diagnostics and independent snapshot protocol.

## New physical evidence

The installed standard eboot matches commit `3b988d0` and VPK SHA-256
`48a22e97691372dbb2c5eb2ab20289f16f0f5a5f422b25bfa60215ac515848a1`.
The new session reaches owner initialization and first-frame target setup,
then stops before completion. The new core is partial: 38 segments, including
relevant stacks, are missing. Its SHA-256 is
`e78b58f4fd327f1f51b5addb3907084020d6d15324540241c21c93f9e8842a9b`.
Raw dumps, firmware bytes, photographs and session logs remain private.

The faulting thread is **VMDNativePaf**, not the app main thread or text worker.
Its r0 is **0x80028186 / SCE_KERNEL_ERROR_LW_MUTEX_NOT_OWNED**. PC is the PAF
assertion trap; LR identifies RMutex::Unlock immediately after an unsuccessful
kernel lightweight-mutex release. Read-only static inspection of retail PAF
locates that release in Framework::Run's main-thread-mutex boundary. The app
main thread is waiting. No complete stack backtrace is available from this core.
This identifies an ownership error class, rather than another malformed target
or evidence of successful native painting.

## Error class and complete adapter correction

The previous lifecycle correction initialized PAF and mutated widgets on the
app thread, but entered Run on VMDNativePaf. Run releases/reacquires PAF's
main-thread mutex while processing a frame. Moving Run alone to a worker does
not transfer the constructor's mutex ownership. The previous host mock modeled
frame signaling but omitted that ownership rule; its passing result did not
cover this crash class.

The adapter now gives one process-lifetime kernel thread ownership of **module
initialization, Framework construction, common resources, plugin/page, all
widget operations, and Run**. MainThreadCallList::Register dispatches bounded
widget-cache updates on that same owner. The app thread only captures immutable
commands and calls the explicit OneFrameMode Update interface with its actual
post-scene framebuffer. It never calls a native widget operation, attempts to
release PAF's mutex, or patches firmware state. The callback checks its thread
ID before any widget operation. No new PAF hot-unload path is introduced.

An acquire/release generation protocol publishes commands only after validating
the framebuffer, and acknowledges their consumption before reusing the queue.
The owner publishes initialization readiness separately; startup waits at most
5,000 one-millisecond delays. Failure/exit/timeout stops further submissions;
app-theme changes and preference persistence run on the app thread. An owner
that finishes late sees the abort flag before processing commands. Initialized
native storage remains alive until process exit, never freed under a worker.

The initial Run condition signal can precede an actual painted frame even
though its active count is nonzero. A second handshake on the first frame only
covers that startup race; a command-generation acknowledgment is also required.
Later frames use one Update. Empty/hidden pages do not drive native idle frames.
No disk/network processing is performed by the per-frame task. If a child
update fails, the validated root is hidden by its owner before the task returns;
the app then restores and persists Legacy and stops native submissions.
The first-frame checkpoint establishes acknowledged command processing and a
returned handshake, not a proof of Sony pixels or correct final finishes.

There are now **32 functions in five weak PAF libraries**. The new callback's
exact ARM C++ signature and symbol were independently compiled against the
pinned reference; its NID/library were checked against both reference metadata
and private retail exports. All 32 import pairs match retail export tables.
Only app-owned declarations and ABI facts are packaged, not Sony headers,
code, assets or extracted firmware. Media decoders, clocks and package identity
remain unchanged: standard 1.4.1, APP_VER 01.05, VitaMediaDeck.

## Primary examples and static cross-checks

- [VitaSDK error enumeration](https://docs.vitasdk.org/group__SceKernelErrorUser.html)
  defines the exact error value.
- [PAF threads/tasks/jobs example](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/blob/9a68b07aa2d7247a756765bf154f292cda20aed3/paf_samples/samples/paf_sample_feature_thread_task_job/src/paf_sample.cpp)
  protects widget mutations made by background threads with the main-thread
  mutex, and uses MainThreadCallList tasks for work on the rendering thread.
  Framework construction, resource setup and Run share their caller.
- [NetStream main](https://github.com/GrapheneCt/NetStream/blob/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source/main.cpp)
  also locks the main-thread mutex before a background job changes UI state.
- [Pinned call-list declaration](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf/common/call_list.h)
  supplies the callback ABI. Declarations alone are not rendering proof.

Private static inspection also follows registration into the owner scheduler,
which is visited before the initial wait and after wakeup in the reviewed Run
loop. This is why unvalidated commands are never published, the initial task
can safely find no work, and the first handshake is not counted as pixel proof.
No firmware code was executed, no Vita3K/JIT/capture helper was launched for
this audit. Previously recorded emulator/JIT failures are historical evidence.

## Checks completed before the new build

- **10,628** actual-adapter checks under ASan/UBSan with real POSIX threads.
  Every mocked PAF API other than external Update enforces the initializer's
  owner, including allocation/hash/style/cache operations and idle/theme hide.
  Run releases/reacquires an error-checking POSIX mutex on that owner. A separate
  negative control proves a foreign unlock returns EPERM, without aborting the
  host. Contract failures in this suite now report an ordinary error exit.
- Startup task visits, initial signal without painting, missing task, wrong
  callback thread, all existing text/layout failures, late/returned Run,
  create/start failure, invalid targets, repeated frame/cache updates, clipping,
  theme switches and hidden idle frames are covered. Failed child updates hide
  the root; no failed graph is accepted as a successful first frame.
- **1,467** actual native panel/L1 dispatch checks, IT/EN and all L1 selections.
- **20,052** chrome checks across eight themes, IT/EN and clock modes.
- **224 QR states**, eight themes/IT/EN/reduced motion, plus editor/log checks;
  37,923,866 pixel/state assertions are not independent physical scenarios.
- Production ARM Release-option syntax with Werror; independent compile-only
  ARM callback ABI probe; all retail import pairs; source/resource/license and
  relative Markdown-link checks. None of these steps creates a VPK.

A pre-build gate records source hashes, results, timestamp and the unchanged
previous standard artifact. Release/LTO, final weak-import table, VPK integrity,
source archives and delivery hashes are checked after building, separately.

## Remaining physical acceptance

Install the replacement standard package and restart Legacy Vita. Confirm the
log passes `first-frame complete; owner commands acknowledged`, then check
visible text and native styles on Home, lists, L1, settings, guide and QR; rapid
navigation, theme switching, player HUD/subtitles and suspend/resume. Report any
new dump together with session_standard.txt. L1+R1 during startup remains the
manual Legacy recovery. Copying a VPK is not installation or a successful run.
Host ownership checks do not prove rendering, GPU coexistence or battery use.
