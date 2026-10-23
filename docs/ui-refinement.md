# Native UI and IPTV refinement — 1.3.0 preview

## Approved Crossfade implementation — 7 September 2026

The approved review now drives native code, not only the browser prototype.
OLED black, graphite `#0C1518`, pale text `#EAF5F0`, mint context `#85E6C4`,
amber focus `#F4C375` and coral errors `#FF6874` are shared theme tokens. The
header is a single graphite surface rather than 48 gradient strips and
decorative telemetry marks. Existing fonts, breadcrumbs, dense gutters,
three-column grids and square catalogue artwork are retained.

Native focus has a finite 120 ms cubic transition that retargets the current
presentation on rapid input. It does not delay logical selection. Home tabs
keep a quiet mint underline while content owns input; content, catalogue and
drawer focus use amber. Local grids draw the moving frame above their cards
so artwork cannot conceal it. Opening a drawer suppresses page focus. The
home rail, radio/podcast catalogues, IPTV channel rows, context menus and
destination picker use shared motion, with immediate Reduce motion behavior.
The previous continuous focus particles/bloom and audio particle lines are
removed; the existing optional cover-color ambient audio animation remains.

Music, podcasts and radio now have **L1 → Player** layout choices: centered,
large cover left, large cover right. Press Right/Left in the left drawer to
switch between Player and Sections, or tap the headings. The choice persists
in unused V3 settings bits without moving existing fields. Covers reuse the
loaded texture; transforms interpolate without reopening media. Missing art
uses a centered text layout and optional author/album rows collapse. Compact
48×44 transport targets replace wide labelled buttons. Radio retains only
Play/Pause; shuffle/repeat remain in R1 and on their hardware shortcuts.

### Remaining page-family rollout

The approved composition now extends across the remaining native page families
on `feature/iptv`. The atlas supplies hierarchy and spacing, not its fictional
media data or simplified action lists. Real protocol controls remain available.

| Native family | Applied composition |
| --- | --- |
| Startup / shared header / text entry | Quiet graphite header and IME backdrop; no acquisition brackets around the logo or input prompt. System keyboard and secure-entry handling remain native. |
| Local and remote grids | Three 302×176 cards per row; 108-pixel artwork band; separate title/detail rails; placeholders fit inside the artwork band. Watched progress and thumbnail caching remain intact. |
| Network management | Sources and download tools remain separate; readable connection summary; connection fields left, protocol/security explanation right; focus drawn above rows. Trust and removal confirmations default to Cancel. |
| Radio / podcast | Three large square cover cards for stations and shows; country/region and genre catalogs remain compact multi-column lists; episodes retain their date/duration column. Home podcast keeps Online/Offline columns and favorites-only behavior. Shared geometry drives hit testing, paging and lazy cover requests. |
| IPTV | Larger square channel covers with adaptive two-column rows, including a reduced visible-row count with mini-player. Link entry and catalog use the shared moving focus. |
| Downloads / destination / QR | Filename has its own upper rail; full-width progress; paused glyph replaces spinner and stale ETA; clean success/error states. Destination selection retains its explicit path and folder actions. QR preserves the working 480-square decode crop and 444-square preview, with status outside the preview and touch Cancel. |
| Video / images / audio options | Compact video transport, separated quality text, 430-pixel options panels; bottom image title/metadata rail; shared finite focus in player menus. Decoder, seek, track and subtitle state machines are untouched. |
| Mini-player / ECO | Quiet mini-player dock without packet decorations; viewport/transport hit regions unchanged. ECO remains black, with its existing dim lock indicator and input policy. |
| L1 / R1 / file actions | Shared mint context, single amber focus and wider options panels; compact file-action modal with safe destructive confirmation. |
| Settings / tab order | Unnumbered tabs; explicit tab versus row input ownership; larger full-aspect subtitle preview; bounded labels/values; moving focus for tab reorder. |
| Controls / About / logs / detail readers | Controls organized into chapters from the existing translated reference, with a scrollable reading pane. Two-column About identity/performance, wrapped specs, quieter tabs and log selection. Generic detail readers retain complete scrollable text. |

No decoder or network transport interfaces, media clocks, subtitle rendering
rules, lock/ECO policy or PS+Start behavior are changed by this UI rollout.
Experimental optical effects from the browser study are not enabled. Hardware
smoothness, full workflow acceptance and playback-plus-download concurrency
still require Vita testing; host layouts are not a frame-time measurement.

Validation includes the optimized native build, sanitizer-backed focus/audio/
settings/guide/file-action/download layout tests, real IPTV/catalog UI input
tests, transport regression tests, and all 720 Home-tab permutations. Host PNGs
use substituted system status and media fixtures; the subtitle preview engine
is substituted in the settings layout harness. They are not release screenshots.

## Design and navigation

The established Inter typography, OLED-black background, graphite surfaces,
jade focus and ice-white text remain. Changes concentrate on readable content,
context-sensitive actions and common touch/controller behaviour.

- IPTV opens saved lists/channels. Start opens Add stream; the URL automatically
  selects HLS, DASH, M3U or file mode. Triangle still overrides the type.
- R1 Options holds Enter link, QR, save, tests, favourites and refresh as
  appropriate. Square still stars a channel, Select stars an opened list,
  and Triangle filters saved entries. Circle returns to the saved collection.
- Radio/podcast discovery uses three-column cover cards; countries have three columns,
  genres two, and episodes one wide column. Home remains favourites-only, with
  separate Online/Offline podcast columns. Scroll rails show the current window.
- R1 Options exposes contextual actions rather than a permanent wall of buttons.
  Episode download/details, URL addition, search, saved entries and catalogue
  navigation remain available. Direct controller shortcuts are retained except
  R1, now consistently Options. L1 and R1 close the opposite panel.
- Options slide with time-based interpolation and a dimmed backdrop. Labels
  move with the panel under clipping; Reduce motion removes the animation.
  Navigation has less decorative noise. Music covers use a simple frame.
- Catalogue covers use their complete original inside a square, with black
  padding for non-square logos. They no longer enter the video 16:9 crop.
  Mini-player artwork uses bounded common image decoding, square compact
  geometry, and fit rather than crop. Video geometry is unchanged.

## Space and orientation pass

- Shared 18 px content gutters replace 48–66 px margins in the media/file
  browsers, IPTV, radio/podcast catalogues and destination picker. Decorative
  side rails no longer reserve or cross the content area. Local and network
  media grids use three 302 px cards, with matching focus and touch geometry.
- The header now reads `VitaMediaDeck / section / parent / current item`.
  Folder context moves out of duplicated title bands. Long trails collapse
  intermediate folders before shortening the current item, use UTF-8-safe
  labels and reserve room for both activity indicators plus system status.
  Paths are informational; they are not new touch navigation controls.
- Video, image, music, radio and podcast players show the media title in their
  trail. Video/image trails respect HUD opacity. Podcast identification is UI
  metadata only; decoder clocks, interfaces and playback timing are untouched.
- IPTV shows eight cards (six with the mini player); radio/podcast list rows
  use the available viewport and compact toolbar. Home still contains only
  favourites. The destination picker shows six folders instead of four, retains
  its explicit destination path and never creates folders automatically.
- English/Italian host layouts include mini-player safe areas and long paths;
  their breadcrumb text uses the production renderer. System status and the
  mini player itself remain stubs, not proof of rendering on Vita.

## Package and transport fixes

The previous VPK had 296 entries, including 255 separate flag PNGs. The new
`flags.pack` contains the 251 two-letter country codes used by the catalogue;
four unused UK subdivision source PNGs remain in the repository. Original PNG
bytes are preserved. One indexed read obtains a requested flag; no runtime
extraction or flag-cache files are created. This removes 254 installation
entries. Actual installation time must be measured on Vita.

The mounted September 7 log showed successful DASH at 768×432/50 fps, followed
by two failed opens with zero buffered segments (one explicit network error).
It did not identify the failing URL, so the precise remote failure is not yet
proven. Improvements made without altering decoder timing:

- Preserve a bounded, printable `#EXTVLCOPT:http-user-agent` player identifier
  for HLS/DASH fetches and saved channels. The supplied IPTV-Italia playlist
  includes this field. Reject control/header injection and ignore arbitrary
  executable options, credentials, cookies and custom headers.
- Allow up to 65 seconds for cancellable initial playlist/segment acquisition,
  instead of cutting a multi-request/retry startup off at 25 seconds. Individual
  HTTP requests retain their 20-second timeout and three-failure limit.
- Log playlist/variant/segment stages, byte counts, elapsed time, HTTP status
  and transport error codes without URLs, payloads or tokens. A failure before
  any buffered segment is reported as a connection error, not a decoder error.

The later HLS transport now supports MPEG-TS, fragmented MP4, separate
audio/video renditions, byte ranges and AES-128 with rotating keys through
FFmpeg and the verified application transport. These additions are covered by
the host checks in [the IPTV plan](iptv-plan.md); they still need a console
retest. Provider DRM and unavailable/expired URLs remain unsupported.

## Verification

- ASan/UBSan: complete square/portrait/landscape/alpha artwork and byte-identical
  reads of all 251 packed flags (233,798 checks).
- Source/session/VOD tests under ASan/UBSan and ThreadSanitizer; 300-segment
  HLS VOD and generated nonzero-timestamp A/V fixtures.
- Host-rendered production IPTV and online catalogue layouts in English/Italian;
  context actions, disabled rows, touch, L1/R1 switching and token redaction.
  These images use a stub system header/player, not Vita screenshots.
- Public Mux HLS: two real FFmpeg readers decode six seconds in RAM, with
  360 video / 259 AAC frames and 23.222 ms initial A/V difference. AAC PES
  packets without individual timestamps use the sample-count clock, matching
  native elementary audio. Host curl is not Vita networking/audio output.
- DASH parser, demux/PTO, live refresh/end, cancellation and backpressure
  regressions pass. DASH has separately been confirmed working by the user.

The next console checks are installation duration, the same failing HLS link
plus the Mux sample, square artwork, touch/controller menus, paused playback
and mini-player navigation. New Vita screenshots are still needed before
replacing the README gallery.

References: [Apple HLS specifications](https://developer.apple.com/documentation/http-live-streaming/hls-authoring-specification-for-apple-devices/),
[Mux public test streams](https://test-streams.mux.dev/).
