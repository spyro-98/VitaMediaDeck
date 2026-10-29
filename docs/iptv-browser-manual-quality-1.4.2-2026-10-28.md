# VitaMediaDeck 1.4.2 — IPTV lists and manual smoothness checks

## Playback

Opening a channel no longer runs comparison playback sessions. Selecting
Automatic also never starts a measurement. A valid favourite resolution or an
existing cached recommendation can be reused without opening trial decoders;
otherwise the normal compatible source selection is used. Available format
metadata is still loaded for the quality/language menu.

The existing **R1 → Video quality → Check playback smoothness** command is the
only production entry point to new comparison trials. Cancelling/skipping an
explicit trial preserves the selection that was active before the command.
The existing stall/cadence scoring and favourite-resolution persistence remain.

## Two presentations

R1 provides **Use side-by-side lists** / **Use separate screens**. The choice is
saved in unused bit 4 of the V4 clock extension; CPU fields occupy bits 0–3.
The 44-byte disk format, checksum and existing settings are preserved. Failed
writes leave the previous in-memory and persisted choice intact. Separate
screens remain the zero/default value.

- Separate screens show saved playlists as a vertical list. Open one with Cross
  or touch to see its channel list; Back restores the playlist selection. Saved
  channels are a separate collection, reachable from R1 and the contextual row.
- Side-by-side mode keeps playlists on the left and the current channels on the
  right. Left/right switches focus, Cross opens a playlist or plays a channel,
  and each pane has an independent pixel viewport. A saved-channel row opens
  standalone saved channels. Unsaved open-once playlists remain available during
  the session without being silently saved.
- Switching presentation keeps the loaded catalogue and channel selection and
  does not fetch/reopen/play anything. Hovering a playlist never fetches it.
- Both presentations use vertical rows with separators and moving focus, not
  the former two-column card grid. D-pad traversal reaches preceding real rows
  before leaving for the toolbar. Touch dragging, inertia, analog scrolling,
  reduced motion, clipping and mini-player clearance remain supported.
- A failed/cancelled list opened from the browser returns to the playlist
  browser instead of unexpectedly switching to the link-entry form. The Add
  flow still keeps invalid input available for correction.

VMD uses the native blue material and PAF glow; the other seven themes retain
their palettes and background scenes. Pane headings have a dedicated gap below
the action row; native labels are bounded away from adjacent icons/columns.

## Evidence and delivery

ASan/UBSan checks exercise both layouts/R1 switches in all eight themes and
IT/EN, mixed saved collections, actual open/play/back flows, saved-channel
access, independent pane scrolling, loaded selection preservation, invalid-list
recovery, manual-only quality trials and preference persistence/rollback.
Navigation/physics, settings pages and native PAF ownership/composition/routing
checks accompany the native build and linked-import/LTO/package audits.

The final package remains **VitaMediaDeck-1.4.2.vpk**, APP_VER **01.06**, TITLE_ID
**VMDK00001**. `build/native` is the single reusable build; current delivery,
logs, host previews, checksums and console-copy receipt are under
`build/releases/1.4.2`. The old delivered 1.4.2 is replaced after checks pass;
1.4.1 remains the single rollback package. Console copy is byte-verified and
recorded separately from installation. Host rendering/build checks do not prove
physical Vita PAF pixels, touch timing or network playback performance.
