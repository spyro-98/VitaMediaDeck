# VMD startup recovery and default upgrade — 26 October 2026

## Verified console result

The installed `b11d5ad` executable matches the preceding standard VPK
`35c6a92ba7eebe32f750c39a28ca5817a2a83da405ee1bcbc441089dbae11bdd`.
Its new session starts with theme **7 (VMD)** and reaches the page checkpoint,
then reports the app-generated `-0x5047` (`initial-widget`). Recovery persists
**0 (Legacy)**. This is a native initialization failure, not a missing theme or
an old VPK. The older successful session started in Legacy and enabled the
native theme later; it did not establish cold-start acceptance for VMD.

The old generic initialization error cannot distinguish a missing busy node
from a rejected Hide result. No complete firmware stack or exact original
failing call is available. Private console logs/settings are preserved only in
local evidence; the public bundle contains hashes and a redacted description.
No console settings, credentials or media were edited externally.

## Incorrect return-status assumption

The pinned [ScePaf-RE Widget declaration](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf/widget/core/widget.h)
declares Show/Hide as returning int32. The app therefore interpreted a negative
return as an error during initialization, page transitions, cached child
visibility and recovery. That declaration is not a reliable status contract on
the privately checked retail 3.74 implementation: both functions finish by
loading the stack-protector cookie into r0, compare it to the saved cookie and
return without assigning a result. Its high bit can therefore trigger false
failure. This conclusion comes from static ARM disassembly, not an emulator,
JIT or firmware execution. The private firmware/code bytes are not distributed.

The common visibility helper now discards that incidental register at **every**
Show/Hide boundary. Import NIDs, argument types and call conventions are
unchanged; no function is called through an incompatible cast. Other native
setters retain their status checks. In the same static inspection, alpha,
size and color explicitly return zero on success; text assignment has its own
result path. Position delegates to its internal typed setter. The change does
not generalize all int-returning functions into void functions.

Missing widgets, failed constructors, invalid targets, geometry/text/layout/
alpha errors and owner/task failures still trigger actual Legacy recovery.
A missing initial busy node now records its typed slot and operation rather
than losing that detail behind the generic error. Sony pages, fonts, native
panels and the existing media/GXM path remain in use.

Public examples were rechecked for the lifecycle and use of visibility calls:
[NetStream](https://github.com/GrapheneCt/NetStream/tree/99db706d860f7126b0b8b6ac1f5a5bbf960055f8/NetStream/source),
plus the previously pinned PAF sample/header inventory. They do not establish
Sony pixels or prove cold-start behavior on this Vita.

## VMD as the upgraded default

The preceding revision set VMD only for fresh profiles. Existing valid
preferences, including Legacy, still won. This revision adopts VMD once when
an older record still selects Legacy. Existing custom themes remain selected.
A previously unused bit in the existing 40-byte V3 record marks that the default
has been applied; no schema-size, theme-ID or tab-order change is needed.

Every explicit theme choice, including native failure recovery and L1+R1 startup
recovery, persists this marker. A later explicit Legacy choice stays Legacy.
A true new native failure cannot cause an automatic PAF retry on every launch.
Migration uses the existing transactional preference writer, preserves all
other settings and commits RAM state only after successful persistence. A failed
write is logged at startup, leaves the loaded preference intact and can be
retried on a later launch. Once migrated, initialization adds **no further disk
writes**. Playback and decoder paths gain none.

If native recovery occurs, Settings now identifies **Legacy (VMD recovery)** and
explains that the app must be restarted before retrying VMD, in English and
Italian. The normal eight-theme picker still contains VMD at stable ID 7.

## Checks and remaining acceptance

The actual production adapter passes ASan/UBSan and TSan with both signs and
several patterns of the incidental visibility register. Cold initialization,
page/child show/hide, reopening and genuine-error recovery are covered. The
preceding adapter fails that same negative control with normal exit 1; no host
crash is intentionally generated. Distinct busy slots and genuine missing
resources are also exercised. Preference tests cover fresh profiles, all older
theme IDs, V1/V2/V3 upgrades, unchanged payload fields, explicit later Legacy,
restart without additional writes, backup recovery and migration-write failure.
ARM Release/Werror, import/resource, standard VPK/LTO and archive/source audits
are recorded in the current validation bundle after execution.

The native UI/background/icon/layout checks from `b11d5ad` remain historical
validation for those unchanged paths. No decoder/clock/asset changes are made by
this correction. This standard 1.4.1 VPK still needs installation and physical
cold-start verification: confirm **VMD**, native first-frame acknowledgement,
no silent return to Legacy, Settings/menus/QR and video/audio playback. Check a
subsequent app restart and an explicit later Legacy selection. L1+R1 remains
available. Copying the VPK to Downloads does not install it or establish any
of these physical results.
