# Startup loading hang and Vita search-thread contract — 2026-10-26

## Console evidence and limits

The user reports the VitaMediaDeck logo/spinner remaining on screen, rather
than the Vita bubble launch screen. USB read-back confirms the installed
`VMDK00001/eboot.bin` matches the preceding standard VPK exactly: SHA-256
`c62cca2c256002fdc88b75a38dc3250de67060fa13324ee08cfc81bf3acfa02c`.
The package SHA-256 is
`3446b1df369eb79d7cc9ad9092ce29c4c901493f93c00295409e6ef4596940e6`.

However, `session_standard.txt` still contains the older owner-command protocol,
illegal podcast priority and generic -0x5049 recovery. It predates that installed
binary and cannot locate the new hang. No newer crash dump was present in the
mounted data directory. Saved settings have disk diagnostics enabled and theme
**Legacy (0)**. Therefore startup outside PAF must also be investigated; this
observation does not establish which theme was active at every attempted launch.
No settings, media, history or provider credentials were modified over USB.

## Confirmed thread API incompatibility

The earlier illegal search priority prevented its raw kernel workers from
starting. Correcting that priority made their startup paths reachable.
`src/library/search.c` created those workers with `sceKernelCreateThread`, then
used `pthread_cond_wait` for their initial idle wait. The previous host adapter
implemented kernel creation using host `pthread_create`, concealing this
platform distinction.

The exact linked dependency was inspected, without changing it:
[pthread-embedded 11d2e5722d98c86f33c908fc47b2cf6e55205db5](https://github.com/vitasdk/pthread-embedded/tree/11d2e5722d98c86f33c908fc47b2cf6e55205db5).
Its Vita [`platform/vita/vita_osal.c`](https://github.com/vitasdk/pthread-embedded/blob/11d2e5722d98c86f33c908fc47b2cf6e55205db5/platform/vita/vita_osal.c)
allocates a private per-thread cancellation/event record in `pte_osThreadCreate`
and installs it through `vitasdk_get_pthread_data`. A raw kernel-thread entry
bypasses this. `pte_osSemaphoreCancellablePend` retrieves and dereferences that
record. Generic `pthread_self` implicit registration does not allocate the Vita
OSAL record. The condition wait reaches the cancellable semaphore path through
`pte_cancellable_wait` when cancellation is enabled by default.

This is a verified reachable incompatibility in the source/dependency contract,
not a captured console stack trace proving the precise new hang instruction.
An idle POSIX wait in a raw worker can fault or fail on this platform; host
POSIX success did not establish compatibility with Vita pthread-embedded.

Only the search pool in app source uses condition waits. Other app waits and
joins were reviewed for this same class: their workers already use
`pthread_create`. Podcast and library checkpoint workers use kernel waits,
callbacks and normal short mutex sections; their corrected priority is retained.
PAF continues to use its existing dedicated kernel owner, with Sony primitives.

## Correction

- Search workers use `pthread_create`, explicit 256 KiB stack attributes and
  matching `pthread_join`. Successful slots are tracked separately, including
  partial creation failures. The platform creates its required TLS/cancellation
  record before entering production worker code.
- Each worker requests native low priority 191 before waiting or fetching. A
  failed priority request is logged, while the correctly registered worker can
  continue at its inherited priority. No provider fetch moves to the UI thread.
- Initialize background playback state before starting the library sampler;
  its lock/state must not be reset after another thread can sample it.
- Persist one-time startup milestones, respecting the existing disk-log option,
  from before graphics through boot assets, playback/history, network and search
  completion/opening Home. `startup: diagnostics schema=2` distinguishes this
  instrumentation from an old retained session. These saves are confined to
  startup; no per-frame or playback writes were added.

PAF rendering, decoder behavior, media formats, clocks, cache/search budgets,
standard identity and user settings remain unchanged.

## Verification before packaging

The production search fixture now distinguishes registered POSIX worker context
from raw kernel creation, verifies all three workers can reach the idle wait
before any query, checks priority/stack, and covers partial/all creation failure
and correct joins. The previous production source fails the new negative control
with a normal exit code 1, without generating a host crash report. The corrected
pool passes under ASan/UBSan and ThreadSanitizer together with actual production
library persistence, local metadata search, late provider replies, cancellation,
cache and cover publication, authentication/account isolation and checkpoint
sampling suites. Additional ARM Release syntax and native adapter audits are
recorded in the standard release validation bundle after execution.

No Vita3K, ARM JIT or emulator was launched. Host thread-context assertions model
a requirement found in the pinned Vita implementation; they do not execute Sony
firmware or capture a physical-console stack. A new native build and archive
checks establish integration, not resolution on the physical Vita.

## Physical acceptance still required

Install the updated standard `VitaMediaDeck-1.4.1.vpk` from Downloads, launch and
confirm the logo gives way to Home in Legacy, then test global search, rapid
query replacement, app closure and relaunch. Verify podcast restart/suspend
resume and background playback independently. Only after startup works, select
Legacy Vita and check its PAF UI/player transitions separately.

If startup still hangs, reconnect USB without reinstalling: the last schema-2
milestone identifies the outstanding phase. Keep `session_standard.txt` and any
new dump. The previous generic PAF fallback is a separate unresolved physical
acceptance item; this correction must not be described as a proven PAF fix.
