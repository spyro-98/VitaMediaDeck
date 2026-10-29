# IPTV programme guide — 1.4.2 follow-up, 28 October 2026

The old programme panel required a declared guide URL plus an exact tvg-id,
rejected listings without `stop`, and limited both compressed and expanded guides
to 8 MiB. The reported 380-channel playlist has no declared guide URL, so its
programme callback was never installed. The current panel always offers the
programme loader for live IPTV, with distinct missing-source, unmatched-channel,
expired/no-programme and network-error messages.

## Source discovery and matching

- Extended M3U headers accept `x-tvg-url`, `url-tvg` and `tvg-url`, single/double
  quotes, unquoted values and whitespace around `=`. Up to four declared guide
  URLs can be tried. Channels retain tvg-name independently of their display
  title, and per-list/per-channel tvg-shift is applied after timezone parsing.
- Recognized `get.php` playlist URLs with username/password can attempt their
  same-origin sibling `xmltv.php`; only those two encoded parameters are retained.
  This is a provider convention, not a guarantee that the endpoint exists.
- Without a guide, an explicitly Italian `.it` channel ID can use the public
  Open-EPG italy3 guide. No playlist URL, stream URL or user credentials are sent
  to the public guide. Other countries and channels without country metadata do
  not silently use an Italian schedule.
- Exact IDs take priority, followed by normalized punctuation/case ID matches,
  then unique tvg-name/display-name/title matches. Parenthesized card suffixes,
  separated numeric channel prefixes and trailing quality tags are excluded
  from display-name aliases. Country and `+1` distinctions remain;
  ambiguous names are rejected instead of choosing the first channel.
- `R1 > TV guide · XMLTV link` configures a persistent guide on the current
  playlist or selected saved channel; reset returns to source/default discovery.
  Failed or cancelled writes retain the previous configuration. A configured
  playlist guide is applied immediately and survives saved-list reloads.
- Recent items retain their parent playlist through the existing library context.
  Missing guide metadata is recovered on the background programme worker after
  the live decoder opens, without waiting for R1 or delaying video startup.

## Reader, cache and presentation

The worker reads plain or gzip XMLTV, allowing 16 MiB transferred and 64 MiB
expanded. Gzip is inflated into XML-reader chunks, without allocating a second
full expanded guide. Existing no-network XML parsing remains; custom entity
references/declarations, truncated XML/gzip, oversized expansion, excessive depth
and cancellation discard partial results. External DOCTYPE declarations are
accepted without loading their DTD.

XMLTV timezone offsets are converted to UTC for comparisons. Short minute-based
stamps are accepted. Missing stop times can be inferred from the following
listing on the same channel; an old programme without a known following end is
not labelled as currently airing. On Vita, presentation uses SceRtc UTC-to-local
conversion, respecting the console zone independently of libc TZ.

The scrollable R1 Programme panel shows the current title, available description,
subtitle/category/episode, start/end, remaining minutes and up to six upcoming
listings with times and available descriptions. It shows the date for listings
outside today. D-pad/analog/touch scrolling continues to use the shared pixel
viewport and inertia in all themes. Square refreshes the guide; Triangle retains
its player-wide mute action. The footer labels Square correctly.

Live guide preload, scheduled updates and Square refresh run on one bounded
background service. R1 only displays cached/pending results. Playback never
enters a full-screen guide loader or joins an old channel's worker. The latest
channel owns the displayed result; channel switches cancel without blocking.
Pending feedback stays inside the panel, and missing matches use a concise
programme-unavailable message without tvg/cache/provider diagnostics.

Four raw validated guide files live in `ux0:data/VitaMediaDeck/iptv-epg/`, outside
the VPK. Slot selection reuses matching sources, then vacant/oldest entries,
without hash-bucket collision eviction. Steady state is bounded at 64 MiB;
atomic replacement temporarily uses a staging file/backup. Public Open-EPG is
reused for 24 hours (its publisher updates daily), other guides for six hours.
Manual refresh bypasses that age after a five-minute minimum. A failed refresh
uses the last valid cached guide and labels outdated information; each read recomputes current/next
from its timestamps, never treating expired data as a current programme.

## Research and verification

Primary references:

- [XMLTV project format](https://wiki.xmltv.org/index.php/XMLTVFormat) and
  [DTD](https://raw.githubusercontent.com/XMLTV/xmltv/master/xmltv.dtd): channel
  references, programme start/optional stop, multilingual titles/descriptions.
- [Kodi IPTV Simple documentation](https://github.com/kodi-pvr/pvr.iptvsimple):
  M3U guide declaration, tvg-id/name matching, XMLTV caching and time shifts.
- [Open-EPG country guides](https://www.open-epg.com/app/epgguide.php): public XML
  and gzip endpoints and daily update policy.

A public italy3 gzip was downloaded directly: 74,824 compressed bytes,
1,023,807 XML bytes, 63 channels and 5,506 programmes, spanning 28–30 October.
No private playlist link was sent to that service. The actual parser matches the
current Rai 1 description/end and six future programmes. Cross-checking the
previously downloaded reported playlist matches all its first eight national
channels, including its differing IDs, to current description/end/upcoming data.
This is a snapshot of guide availability, not permanent coverage for every station.

`run-options-tests.sh` exercises production readers/cache under ASan/UBSan and
TSan: large compressed expansion, actual guide fixture, matching, country/+1
isolation, ambiguous aliases, missing end, out-of-order schedules, cancellation,
entities, cache reuse, failed refresh and metadata/provider discovery.
`programme_flow_test.c` checks the actual R1 configuration/rollback/reset and
recent-item routing. `player_panel_test.c` renders current and scrolled schedule
views in all eight themes and IT/EN. Storage tests round-trip name, shift and
multiple guide URLs. Preference-restart, manual-quality, navigation, playlist and
player-feedback regressions remain passing.

Host previews substitute native firmware text/control pixels. The VPK is compiled
and audited separately; this follow-up has not been copied, installed or verified
on the physical Vita while USB is unavailable.
