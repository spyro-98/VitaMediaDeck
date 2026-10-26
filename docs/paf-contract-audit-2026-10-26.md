# PAF integration contract audit — 26 October 2026

> Follow-up: the Show/Hide status-check policy below is superseded by the
> [retail visibility-return correction](vmd-startup-default-2026-10-26.md).
> The historical header audit did not establish a meaningful return status.

## What the new console session establishes

The installed eboot matches standard commit `b947d3c`, VPK SHA-256
`6b51c699d9ff0a3311420063176dd80dba6c07c1b8a263021b7850ad0b85cc08`.
The session completes native initialization and the first owner-command frame,
then records `unavailable 0xFFFFAFB7` and explicitly restores Legacy.
That value is **our -0x5049**, not a Sony/PAF error code. The previous adapter
uses it both for renderer exit and for a missing same-frame generation match.
The log lacks the counters needed to distinguish these branches. It therefore
does not prove either a new mutex crash or the exact triggering branch.
Raw session data remain private. No new emulator or firmware execution is used.

The earlier mutex ownership fix remains necessary: module/framework setup,
resource/page creation, widget mutations and Run still share one owner thread.
The new audit corrects independently demonstrable assumptions in the frame/task
protocol, adds diagnostics to distinguish a stopped renderer from a stalled
task, and checks the rest of the integration against public references.
It does not claim to have reproduced Sony pixels on the Mac.

## Public references reviewed

| Reference | Pinned revision | Relevant practices and limits |
| --- | --- | --- |
| [ScePaf-RE](https://github.com/GrapheneCt/ScePaf-RE/tree/b76d6ae00516b107d37e7e8a1a16211e3b729a07) | b76d6ae | Framework/plugin/page lifecycle, widget/string/ID types, callbacks, external framebuffer record, exports; reverse-engineered declarations have known retail differences |
| [PAF component and samples](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/tree/9a68b07aa2d7247a756765bf154f292cda20aed3) | 9a68b07 | Loader plus text/button/plane/busy/input/thread-task-job and other sample patterns; initialize before Run and keep blocking work off rendering tasks |
| [Thread/task/job sample](https://github.com/Princess-of-Sleeping/vitasdk-paf-component/blob/9a68b07aa2d7247a756765bf154f292cda20aed3/paf_samples/samples/paf_sample_feature_thread_task_job/src/paf_sample.cpp) | 9a68b07 | Background widget access explicitly locks the main-thread mutex; main-thread tasks execute through PAF's scheduler |
| [NetStream](https://github.com/GrapheneCt/NetStream/tree/99db706d860f7126b0b8b6ac1f5a5bbf960055f8) | 99db706 | Owner-created Framework/Run, plugin callbacks, protected background UI access, persistent main-thread player/surface tasks, retained media surfaces |
| [ElevenMPV-A](https://github.com/GrapheneCt/ElevenMPV-A/tree/ae6583efa3ee55bc0d84b9d9c5638eea6f357491) | ae6583e | Native player/application lifecycle and memory-budget-specific surface/text pools; older ABI/member naming cannot be copied blindly |
| [SelfLauncher](https://github.com/Ibrahim778/SelfLauncher/tree/aa8a5e59ba2e0b9e77fbdf29a10a23139cc5917c) | aa8a5e5 | Common-resource/plugin startup, callback-created UI, Framework Run and process exit; older ABI naming |
| [libvita2d_sys_paf](https://github.com/GrapheneCt/libvita2d_sys_paf/tree/011bc559c0e46ad9ddd6088f3a76e4f0bc57fa2b) | 011bc55 | Draw calls inserted in PAF's widget draw queue and using PAF's graphics context; not a drop-in replacement for our existing media contexts |
| [VitaSDK PAF](https://docs.vitasdk.org/group__ScePafUser.html), [thread manager](https://docs.vitasdk.org/group__SceThreadMgrUser.html), [kernel errors](https://docs.vitasdk.org/group__SceKernelErrorUser.html) | checked 26 October | Public module/thread/error declarations; the PAF page is not a complete C++ UI integration guide |
| [SDL Vita priority correction](https://discourse.libsdl.org/t/sdl-vita-fix-thread-priority-add-support-for-thread-name-and-stack-size/32839) | upstream change | Documents normal user priority 160 and low priority 191; used for the separate worker failure in this session |

These repositories were read as references, not incorporated into the app.
The audit covers relevant loader, lifecycle, task, widget, media and graphics
paths; a file hash inventory does not mean every line of every external app was
reviewed. No public example reviewed establishes a one-to-one task callback
promise for each external OneFrameMode Update.

## Adapter-wide findings and corrections

### Task scheduling and command lifetime

The adapter required `applied_generation == requested_generation` immediately
after every Update. That was an app assumption, not a demonstrated PAF contract.
The host mock always delivered the task in that cycle and therefore could not
expose normal delayed visits. Sharing the capture array was safe only under
that same assumption: relaxing the check alone would create a lifetime race.

There are now three bounded snapshots: app capture, pending mailbox and owner
working copy. Two extra 512-command buffers cost 568 KiB of static application
memory; no per-frame heap allocations are added for this transfer. A try-lock
protects only the bounded memcpy, never native calls. The owner copies its own
snapshot before applying widgets. Newer UI snapshots replace obsolete pending
ones. Playback/seek commands and media data do not pass through this mailbox.
A busy producer/consumer skips an obsolete UI snapshot without spinning.

A skipped task visit no longer triggers Legacy. A distinct **-0x504b /
task-no-progress** detects two seconds without progress during continuous active
Updates. Idle gaps over 250 ms, suspend/resume gaps and clock discontinuities
start a fresh interval. First-condition priming runs once, independently from
whether a task has consumed a snapshot. The first completion checkpoint requires
an actual nonzero acknowledgment, rather than just an accepted Update.

### Thread and graphics phases

All PAF object operations remain on the initializer/Run owner, with an explicit
thread-ID guard. CPU widget changes are also restricted to the app's post-scene
flush window: a scheduler visit while the app owns a vita2d scene does no work.
First initialization requested inside an open scene is deferred to the next
pre-scene boundary. The app calls the external Update interface with the actual
framebuffer/sync descriptor after EndScene, before display swap.
The scene gate is not an extra PAF mutex or an attempt to transfer PAF ownership.

The CommonDialog record is still converted field by field to the 32-byte PAF
OnUpdateData record; no 96-byte reinterpret cast returns. Invalid targets are
rejected before publication, now with **-0x504c / invalid-frame**. Native/widget
work never receives app capture memory that can be overwritten by the next draw.
Media, cover/camera textures and subtitles retain their app-owned GPU path.

### ABI, return values and geometry

- **ReorderChild returns bool**, not an errno. Its app-owned declaration now
  matches; false/no movement is not treated as a failure.
- The boolean text-layout argument now uses bool, matching its native overload.
- Show/Hide/SetShowAlpha statuses are checked for both reused widgets and page
  transitions, including initialization. Return values from busy Start/Stop are
  void, while ChangeButtonState is an unsigned state result, not an errno.
- Recovery attempts to hide the validated root on its owner; a Hide failure is
  logged separately and future native submissions stop. This cannot promise
  recovery from a firmware assertion inside a call or from a failed Hide itself.
- Four-lane size/position/color storage is explicitly 16-byte aligned. The
  conservative retail Vector2 storage and known text-layout IDs remain intact.
- Hard-float ARM calling convention, UTF-16 width, three-field PAF strings,
  ID layout, opaque constructor sizes, property IDs and plugin/input/mode
  offsets are independently checked against the pinned reference declarations.
  Existing documented retail exceptions are not overwritten by stale headers.

### Diagnostics, build dependencies and unrelated worker failure

Recovery logs now include a named reason, active Run count, exit/result status,
requested/applied generations, callback count, submitted frames and initial
handshake state. Widget failures retain typed operation/slot/native status.
These records contain no source URLs, media labels or credentials. Disk writes
remain rare initialization/recovery checkpoints, not a per-frame trace.
A stopped renderer remains **-0x5049 / renderer-unavailable**; a wrong widget
thread remains **-0x504a**. These are now distinguishable from task delay.
CMake tracks import metadata/generator changes so the next build regenerates
app-local weak stubs before linking.

The same console session reports `podcast checkpoint worker unavailable:
0x80028023` (illegal priority). Podcast saver, unified-library saver and search
workers used 0x10000140. They now use documented low user priority **191**.
The old mocks mirrored the invalid constant; they now require the valid low
user range. This is an independent startup defect, not evidence PAF caused it.
Persistence/search remain on background workers and the write budget is unchanged.

## Complete local integration scope

Reviewed: native_vita.c/.h, native_paf_abi.h, centralized font dispatch,
components, focus glow, loading, L1 sidebar, subtitle opt-out, runtime/startup
boundaries, generated weak-import metadata/stubs, CMake links and app-owned XML
resources. UI enqueue functions do not call PAF; they only capture app state. Native module loading is lazy and refuses a foreign Framework; app-owned opaque storage/strings use the native allocator and remain alive until process exit. AppUtil/CommonDialog initialization and global C++ allocation overrides from standalone samples are not copied into this existing C/media runtime. Locale/input remain app-owned; native input is disabled/configured for external control.
All actual imports are confined to the native adapter, with one external Update
boundary. The table below accounts for every imported function.

| ABI boundary | Functions | Ownership / contract |
| --- | --- | --- |
| Framework parameters/construction/resources | `fw_params`, `fw_create`, `fw_instance`, `fw_common` | Owner, constructor checks and bounded heaps |
| Framework loop | `fw_run`, `fw_update` | Run on owner; Update on external app thread after EndScene |
| Scheduler | `main_task_register` | Registered on owner; delayed visits allowed, phase-gated |
| Plugin/page | `plugin_params`, `plugin_load`, `page_params`, `page_open` | Owner; synchronous setup, checked plugin/page, process lifetime |
| Allocation and IDs | `allocate`, `id_hash` | Owner; native allocator and borrowed const input during calls |
| Creation/lookup | `widget_create`, `widget_find` | Owner; case-sensitive factory keys and typed bounded pools |
| Geometry/style | `widget_pos`, `widget_size`, `widget_color`, `widget_alpha` | Owner working snapshot; aligned vectors, property IDs and checked status |
| Visibility/order | `widget_show`, `widget_hide`, `widget_order` | Owner; checked visibility status; bool order is not errno |
| Text/button | `button_text`, `widget_text`, `button_state` | Owner; copied UTF-16, class-specific label object, state not errno |
| Text attributes | `text_size`, `text_color`, `text_layout_int`, `text_layout_bool`, `text_layout_vec` | Owner; typed overloads, return checks, known retail layout exceptions |
| Busy indicator | `busy_start`, `busy_stop` | Owner; void API, reuse validated resource instances |

## Validation before the next build

- **14,204** actual-adapter checks under ASan/UBSan and ThreadSanitizer: delayed/skipped callbacks over
  150 frames, missing-task timeout, one-hour virtual suspend gap, immutable
  snapshots, latest-state cancellation on theme change, mailbox contention,
  visits during an app scene, deferred initialization, native setter failures,
  all preceding layout/text/frame regressions and actual POSIX mutex ownership.
  The explicit false-return reorder case is accepted as a no-op.
- **1,467** native panel/L1 dispatch checks, IT/EN and every selectable row.
- **20,052** shared chrome checks across eight themes and **224 QR states**
  plus editor/log coverage. The 37,923,866 pixel/state checks are not independent
  physical test scenarios or Sony native captures.
- Podcast saver: **143** persistence/lifecycle checks and **153** real MP3
  CBR/VBR resume checks, under ASan/UBSan and thread sanitizer.
- Unified library/search: concurrent journal, metadata, cache, progressive
  provider results, replacement/cancellation and trust/connection suites pass
  under ASan/UBSan and thread sanitizer; worker priorities are bounded.
- Independent ARM C++ ABI assertions; actual changed C files checked with
  Release options and Werror; all **32** NID/library pairs still match the
  inspected retail export table. No import/resource/dependency version added.
- Resource/license/source audits, documentation links and source hashes are
  recorded in a pre-build gate before creating any new standard VPK.

## What is still not proven

Most public apps let PAF own their entire render loop/context. Our retained
Mode_CommonDialog/OneFrameMode composition is a hybrid with an existing media
renderer; normal-mode examples and libvita2d_sys_paf do not validate that whole
hybrid by themselves. The declared mode/frame ABI and private read-only static
flow support its use, but are not physical GPU or timing acceptance. Replacing
the media contexts wholesale with that other library is not a drop-in fix.
No Sony certification or complete proprietary PAF manual is available here;
public reverse-engineered headers have version/retail differences.

The new log cannot retroactively identify which old -0x5049 branch fired.
The mailbox/contract fixes are verified regressions, not proof that this exact
console session's triggering branch has been reproduced. A blocking firmware
Update/assertion cannot be made safely interruptible by the progress watchdog;
it only detects a task that stops progressing while Updates still return.

Install the replacement standard VPK and confirm it stays in Legacy Vita past
startup and rapid navigation. Check native text/focus/clipping on Home, L1,
provider/file lists, settings, guide and QR; switch themes with loading pending,
then audio/video HUD/subtitles, suspend/resume and exit. If recovery occurs,
retain session_standard.txt: named reason and counters now distinguish its
cause. Copying the VPK does not install it. Standard identity remains 1.4.1 /
01.05 / VitaMediaDeck; decoder behavior and clocks are unchanged.
