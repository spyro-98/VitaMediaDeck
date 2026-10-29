# VitaMediaDeck 1.4.2 — native blue menus and independent themes

This revision replaces the rejected flat, globally blue menu presentation.
Only the default VMD theme (stable ID 7) uses the blue Vita material. Legacy,
Ghost Mesh, Deck 90, Signal, Aperture, Relay and Mosaic inherit their own
surfaces, text, separators and focus effects. Full-page menus preserve those
seven themes' existing background scenes instead of covering them with blue.
The light Signal palette and its dark media-HUD override remain independent.

## Native finish and composition

The default uses blue drawer gradients and luminous separators. A PAF `plane`
supplies the firmware's `_common_texture_list_glow_70px` selection material;
labelled native controls use `_common_texture_list_70px`. The old option-menu
button textures are no longer used for list focus. When PAF is active the custom
GPU selection wash and feathered outline are skipped. The owner reorders the
native focus behind captions even when the app submits its focus last. No Sony texture is extracted or included in the package.
Native panels now instantiate `plane` with `vmd_panel`, rather than empty Button
widgets. Actual native labelled buttons retain their own typed label handling.

Plain menu captions use bounded PAF Text commands in VMD; mixed controller
sprites retain the immediate renderer and its exact spacing. Caption windows
are explicitly bounded by their component/column, so painting an adjacent icon
or value cannot hide a caption with an erroneously screen-wide text window.
L1, R1 and page content retain separate stable widget scopes. The additive glow
contains no opaque background and does not obscure controller icons.

All native widget operations remain on the PAF owner thread. The existing
bounded asynchronous frame mailbox, CommonDialog handshake, failure handling
and single-owner mutex protocol are preserved. Eight bounded glow slots reuse
native widgets while their floating-point geometry moves across row gaps;
reduced motion remains respected.

## Resource and package

The original symbolic resource is compiled with the pinned psp2cxml-tool into
2,560 bytes, SHA-256
`b0ba4b0dcba4516b72bba4ca1cb34892462de99ae2559c0697cc8739924221b9`.
XML and embedded-source hashes in `release/DEPENDENCIES.lock` are updated and
verified by the native audit. All 37 firmware imports remain weak.

Human/build/about/client version: **1.4.2**. Vita package revision: **01.06**.
TITLE_ID remains **VMDK00001**. Package: **VitaMediaDeck-1.4.2.vpk**.
The reusable compilation lives in `build/native`; the delivery is
`build/releases/1.4.2`, reached through `build/releases/latest`.
Dependencies and licence/source bundles remain separate. One 1.4.1 rollback VPK
is retained. User artwork, source edits, console settings and media are preserved.

## Verification boundary

ASan/UBSan host tests cover all eight palettes, preserved theme backgrounds,
visible IPTV captions, prompt spacing, continuous focus crossing row gaps,
L1 navigation/touch/inertia, settings and IPTV quality/language/programme flows.
The real native adapter with mocked ABI verifies Plane creation, float movement,
reuse, caption/texture layering, modal occlusion and owner-thread boundaries.
The host reference renderer now interpolates vertex colours as GXM does;
previously its flat first-vertex colour could not validate gradients.

Native compilation, resource regeneration, linked-import/LTO audits, VPK
identity/icon/ZIP and release checks accompany the package. The delivery
manifest records whether the current VPK was copied and read-back verified.
Host PNGs do not render Sony's firmware texture or native text. Physical PAF
appearance, frame timing, touch feel and playback remain console acceptance
checks; copying a VPK does not install it.

## Current controls, persistence and startup

Circle is Back in IPTV. The saved playlist/channel collection switch is selected
with Cross or through R1; it no longer displays the Circle shortcut. R1 actions
are categorized and ordered by playback, collection, presentation, connections,
management and information. The file, network, audio, video and image drawers
include the same category vocabulary; the image information drawer no longer
puts empty panel controls behind each value.

Radio supports persistent 1x3 / 2x3 layouts from R1 in all eight themes. Each
layout reserves its theme's tabs and mini-player bounds. Channel resolution and
audio language preferences have a separate per-channel store; unavailable
fields fall back independently. Signed rendition IDs can be matched again by
resolution/language metadata. Unrecoverable data is not overwritten and a valid
backup can be restored. Existing choices are retained when a source temporarily
omits them. Quality comparison still runs only when explicitly requested in R1.
Triangle toggles global mute in the audio/video players, preserving the prior
volume and leaving repeat in R1. Programme refresh uses Square.

The app-owned VMD wave mesh samples 61 segments instead of 19, with bounded
low-frequency quintic noise controlling its thickness over roughly 900 pixels
and 12 seconds. Its shape and periodic endpoints are tested mathematically;
reduced motion remains static. This wave mesh is app-rendered, not a claim of
firmware wave rendering. No proprietary wave parameter patch is installed.

PAF initialization is polled across frames instead of sleeping on the render
thread. Library/history/network initialization runs in the existing pthread
loader. The runtime caches immutable packaged font bytes, creates only three
initial UI atlases, yields loading frames between native font tiers and prepares
subtitle-only atlases on demand. Cached local catalogues remain visible while
the existing scanner refreshes them. Library replay now reads 64 KiB blocks
and uses a table-based CRC with identical on-disk checksums. Large valid journals
are compacted by the checkpoint worker after replay, reducing future cache
reads. A synthetic 12.6 MB legacy journal replays with 194 storage reads in the
host test, preserving every item and detecting corruption/short reads. The
existing console log recorded 2,353 ms in the old playback/history stage; no
physical startup timing of this new binary is claimed.

Tests: `tests/startup/run-host-tests.sh`, native PAF adapter/dispatch, IPTV
preferences and player/browser flows, density changes in all themes, shared
settings/audio layouts, spacing and navigation. Host previews substitute native
firmware pixels. The exact package and console readback are recorded separately
in `build/releases/1.4.2/manifest.json`.


## Photo follow-up: chrome, categories, IPTV collections and type clarity

Home tabs/search are back inside the 54 px chrome in Legacy/VMD; the Home
heading starts below it. Navigating inside local folders replaces the tab rail
with the contextual section/path. Other themes retain their own navigation.
Selectable dialog/catalogue actions no longer each display a Cross icon; a
single common hint explains directional selection, Cross confirmation and
Circle cancellation.

Channel saving is explicit. “Save channel” stores a link without a favourite
star; adding a favourite also stores that link, and removing its star keeps
the saved link. No television programme recording is implemented or implied.
Existing video/audio choices survive duplicate saves and failed writes.
Channel cards use two columns beside playlists and three columns when a
playlist occupies the full page. Playlist collections remain one column.
D-pad navigation, touch targets and continuous scrolling share that geometry.

Every R1 category has a dedicated 34 px non-interactive slot above its rows.
Shared drawers and custom video/audio/local/image drawers use the same offset
calculation for drawing, focus and touch. Focus moves continuously across the
category gaps; headers cannot activate an action.

Native text origins are rounded to physical pixels, without rounding animated
focus transforms. The Latin font dispatcher resolves an independent FreeType
atlas for each actual size and family, including intermediate header sizes;
it no longer silently scales glyphs from another size. Existing immutable face
bytes are shared, and all extra atlases close before those bytes are released.
Sony’s renderer still needs physical visual acceptance; host fonts are substitutes.

Channel catalogue import has its own 8 MiB body limit; HLS/DASH playback manifests
retain their separate 256 KiB limit. A 512 KiB catalogue regression now parses
successfully, while oversize bodies and malformed data remain rejected. The console log identified “Peppenamir”: its QR service returned a 308084-byte
HTML landing page, exceeding the old 256 KiB cap. With the user's explicit
network permission, the declared M3U destination returned HTTP 200, 64138 bytes
and 380 channels without truncation in the real parser. The resolver is shared
by the IPTV screen and library search, independent of the QR provider. It follows
one unique static M3U/M3U8 anchor or a declared HTML meta refresh, with at most
three HTML hops, cycle detection, cancellation and the existing URL policy.
It never executes page scripts or chooses between distinct ambiguous playlists.
This is a host reproduction of import, not physical playback acceptance.
The 512-channel catalogue entry bound and 4096-byte line bound remain in place;
large-entry pagination is not claimed by the byte-limit correction.

Current regression evidence: `tests/ui_chrome/run-context-tests.sh`,
`tests/ui_chrome/run-host-tests.sh`, `tests/startup/run-host-tests.sh`,
`tests/navigation/run-host-tests.sh`, `tests/iptv/run-ui-tests.sh` (including
`saved_membership_test`) and `tests/iptv/run-host-tests.sh`, `tests/iptv/run-catalog-redirect-tests.sh`, plus the existing
native/font/prompt, settings, player and full theme suites.


## Playlist input and Add action follow-up

The IPTV text helper previously used immediate-only drawing, so native PAF
panels could cover its captions. It now uses the shared bounded native text
path. The URL field provides a label, URL placeholder and Select-to-type/edit
affordance. The native routing regression verifies all three captions above
the field panel, including the URL with its query removed from display.

Add and open validates the list, saves it under an automatic host-based name,
and focuses its channels in either presentation. No naming keyboard is opened;
previously saved names survive re-addition, and Rename remains available later.
If storage fails, the form retains the URL and primary action for retry.
The production flow suite passes 903477 checks across eight themes and IT/EN,
including manual/QR paths, both presentations, cancellation, invalid inputs,
write failure/retry and open-without-saving. Actual native appearance still
requires physical verification after installing this package.


## Persistent artwork follow-up

IPTV, radio, shows, feeds and episodes share the indexed raw artwork cache.
The service startup worker indexes existing digest files; UI peek/due lookups
only read memory and never wait for network or filesystem work. Every visible
cached cover path is restored before considering a pending fetch. Temporary
failures do not permanently disable a cell. Failed metadata discovery/thread
starts have a short retry delay, while failed HTTP refreshes back off and keep
the previous image.

Raw artwork is bounded at 256 MiB / 4096 entries (2 MiB per response). It refreshes
after 24 hours on the existing worker, preserving old bytes on failure. Changed
image bytes receive a new digest revision path, published before the old file is
removed. Persisted references resolve to the latest cached revision. The old GPU
image remains available until the new one has decoded/uploaded. No provider
script execution or renderer-thread HTTP is introduced.

Artwork has 96 independent RGB565 texture slots, separate from 12 video preview
slots and 24 flags. Artwork/flags survive playback handoff; video previews are
still released before decoder startup. Visible covers take priority over video
frame extraction without mutually cancelling each other. Decoded artwork uses
digest filenames rather than colliding in 128 video slots, bounded at 2048 files
(about 153 MiB). Changed image URLs retain their old artwork path for the same media entry until the replacement texture is ready.

Host checks cover 80 retained covers through video churn, player round trips,
old-image retention during revision upload, video preemption, fresh/stale/offline
raw-cache behaviour, memory-only lookups while publication is locked, >128 images,
byte/file bounds, a cold process restart, and all visible IPTV/audio paths while
a network worker is busy. Source/build/package evidence remains distinct from
physical scrolling smoothness and network availability on Vita.


## Single IPTV action rail and category spacing

The browser now has exactly one rail: Add, Search/count and Options. The former
Play / Save-or-Link-saved / Playlists row is removed from rendering, hit testing
and D-pad navigation; obsolete focus IDs/helpers are removed. Play/open works
from the selected card with Cross or touch. Save, collection switching and
filtering remain in categorized R1 options; fresh saved channels do not produce
an inactive “Link saved” option or repeat that caption on cards in Saved channels. “From playlist” is no longer repeated on every card; only an independently saved, unstarred channel in a mixed playlist needs a saved-link caption.
Redundant Add/Search entries are removed from the listing's R1 menu.

Full-width cards now start at y=116 and split-view cards at y=152, reclaiming the
52 px action row. The surviving Add/Search/Options IDs are 4/5/7; every visible
control and all real cells are reachable in the graph regression, including
partial rows and empty collections. Touch uses the corresponding three controls
and the same card geometry. No action ID for the removed Filter row remains.

Category slots increase from 32 to 34 px and their title baseline moves from
+20 to +25 px: this adds 5 px after the preceding separator and removes 3 px
before the first item. Shared/custom drawers, focus and touch use the same
heading geometry in all themes. Chrome regression checks the exact offsets;
host screenshots remain substitutes for firmware rendering.


## Persist confirmed IPTV choices before exit

The former owner saved the active preference only after `run_live` returned.
A process terminated during playback therefore lost its latest selection.
The native player now notifies the IPTV owner immediately after successful
decoder startup, before its interactive loop. Only confirmed normal playback
commits; failed opens and smoothness trials do not save a candidate. Resolution
uses the confirmed decoder output and immutable variant metadata; explicit audio
uses its language/name, while source-default audio stays source-default.

The persistent, non-expiring user file remains
`ux0:data/VitaMediaDeck/iptv-playback.json`, separate from favourites and the
expiring measurement cache. The VPK does not include or replace that file.
Vita writes use `sceIoSyncByFd` before publication and `sceIoSync` after the
rename, retaining the prior valid file as backup. Host tests use `fsync`.
A persistence failure is shown without stopping otherwise valid playback.

When a playlist supplies a channel ID, the record also has a hashed identity
scoped to that playlist. Rotated channel URLs can then recover the same choice;
legacy URL-only records migrate without replacing an unavailable preference
with its temporary fallback. Both raw URLs and stable IDs remain hashes in the
JSON. Channels without such metadata retain exact-URL identity.

Tests cover the actual play-link owner and JSON store in separate processes:
select 1080p/Italian, commit at startup, terminate with SIGKILL before player
exit, then restore with changed channel/variant/audio URLs. Another process
lacks 1080p and falls back without overwriting the choice; the following process
restores it when available again. Storage tests cover legacy migration, backup
recovery, failed writes and retention of previous data. These are host process
checks, not an installed-console or power-loss acceptance test.


## IPTV channel name ordering

R1's View category offers one Channel order row: Playlist order or Name (A–Z).
The choice persists in unused V4 extension bit 6, without a record-size change.
The name comparator folds case/Latin accents and compares digit runs naturally;
equal keys retain their source order. Only the filtered channel index array is
sorted. Playlist collections, catalogue metadata, artwork worker indices and
playback identities are unchanged. Sorting preserves the selected channel and
reveals its new row, in both presentations and all themes. Failed preference
writes retain the prior order. Dedicated tests cover filters/favourites, ties,
source-order restoration, actual R1 D-pad routing and fresh settings reloads.
