# Pixel viewports, flat menus and IPTV quality — 2026-10-28

Implemented in the local checkout; no console installation or remote publication.

## Navigation and menus

The shared page viewport now accepts direct pixel dragging and an inertial release,
continuous left-stick movement, and animated D-pad reveal. It is used by the local
catalogue, unified library, local and remote file browsers, destination picker,
saved network sources and online radio/podcast catalogues. The same viewport
coordinates drive drawing, retained partial rows and touch hit testing. New
collections reset their viewport; keyboard navigation cancels inertia.

Settings, personal-media server lists, generic R1 actions, IPTV choices/programme,
guides, long text and logs use the same pixel physics. L1 lists and the Legacy/VMD
home tab rail also support inertial drag. Fixed grids/wheels whose entire contents
fit remain spatially navigable without artificial overflow. Reduced-motion mode
keeps direct pixel dragging and stick input, while disabling inertia and springs.
The eight themes share this behavior. Aperture's content viewport ends before its
bottom section rail. Other viewports reserve headers, footers and mini-player space.

Repeated action buttons, settings rows and player option rows now use dark-blue
flat rows with cyan separators. Controller icons have no additional rectangular
key backing. Icon-only close controls have no outer button surface. Labels retain
reserved icon width and independent text fitting. Existing themed media cards,
fixed spatial dashboards and section layouts retain their theme geometry.

IPTV Up uses absolute channel indices; the first visible row of a scrolled view
cannot prematurely hand focus to the toolbar.

## IPTV preferences and measurement

Sources publish their actual selected video/audio IDs and output dimensions before
startup becomes visible. Favourites save compact stable choice keys and the output
resolution, including an automatically chosen rendition. Existing saved files are
compatible; writes preserve atomic replacement and failure rollback. If a saved
variant disappears, playback reports it and returns to automatic selection.

On a channel with multiple supported variants and no current result, automatic
selection plays each candidate in a fresh session, warms up for 2 seconds, then
observes 8 seconds of presentation. It compares actual presented timestamps,
wall time, audio progress, held pictures, cadence gaps, dropped frames and A/V lag.
Declared FPS is not used as a smoothness score. Among similarly smooth outputs it
prefers the larger picture. Results expire after 48 hours and are invalidated by
variant, backend preference or clock-setting changes. Interrupted/inconclusive
trials are not persisted. Circle stops comparison and returns to viewing. R1
allows explicit quality/language choices and an explicit repeat of the comparison.
The comparison can take roughly 10 seconds plus startup per available candidate.
This is a bounded sample of current conditions, not a guarantee for future network
or decoder behavior.

## Decoder evidence and mitigation

Read-only inspection of the mounted Vita's current session log found:

- HLS H.264 1920×1080 and AAC 48 kHz were successfully identified.
- The direct-output pool reserved 60 MiB before native codec initialization.
- The codec reported `allocate frame buffer failed`; software playback followed.
- One software sample advanced at 0.275× real time with about 3.7 seconds of lag.

The high-resolution direct-output pool now uses 12 surfaces (36 MiB at the observed
1920×1088 texture size) instead of 20. Its presentation queue is reduced accordingly;
GPU-fenced ownership and teardown remain intact. 720p retains its existing pool.
Manual decoder changes on live channels create a fresh source/session rather than
reopening an aged bounded cache in the middle of a GOP. The log supports a memory
allocation failure; it does not establish that HLS/H.264 is inherently unsupported.

## Verification boundary

Host checks cover pixel physics across all eight themes, absolute-index navigation,
IT/EN rendering, catalogue flows, long choice/programme panels, persistence/rollback,
smoothness scoring and calibration ownership, actual HLS variant remux/decode,
source cancellation and HW/SW adapter buffer lifecycle. Native builds and package
checks are recorded beside the delivered VPK.

Physical acceptance is still required for Sony PAF pixels, Vita touch/stick feel,
1080p native allocation on the reported channel, and comparative playback speed
under actual network, decoder and clock conditions. Host software decoding and
simulated presentation traces do not prove Vita hardware performance.
