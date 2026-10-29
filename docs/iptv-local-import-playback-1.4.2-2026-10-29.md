# IPTV content import and current-console diagnosis — 29 October 2026

## Importing files on the console

IPTV → Add → Import file opens a storage picker. R1 also offers Import file.
Choose a file in ux0: or uma0:; Triangle switches storage. Cross opens folders
or selects the focused file; Start selects/opens that focused entry. Import is
read-only: it saves a reference to the chosen file in ux0:data/VitaMediaDeck,
without copying, modifying or deleting the original. Moving/removing the source
file requires importing its new location.

The importer identifies content independently of its extension. A master/media
HLS document is one channel, not a channel directory. An MPD is one DASH channel.
M3U/M3U8 channel catalogues, plain text URL/path collections, PLS, XSPF, ASX and
InternetShortcut .url files are supported. UTF-8 (including BOM) and BOM-marked
UTF-16 LE/BE text are accepted. The file may contain multiple channels and links
to multiple other lists; relative paths resolve against the containing file.
Opening a child list preserves its parent for Back, with cycle detection and a
maximum of eight parent levels. A misleading .m3u8 catalogue is also recognized
when opened through a channel reference. The same classification applies to QR
and typed network URLs, so an HLS stream named playlist.m3u is saved and played
as a channel without a second naming keyboard.

A collection is saved once after successful parsing; individual child channels
can be saved separately. The original saved-reference/favourite distinction is
preserved. Files remain bounded: 20 MiB catalogue text, 512 displayed entries,
128 saved references, and separate playback-manifest/segment limits. Invalid
files, malformed text/XML, entities and inaccessible storage return errors.
Remote manifests cannot read console files. Local resources are limited to
ux0:/uma0:, with normalized relative paths and rejection above the mount root.
The HLS demuxer receives file:-wrapped local references, but every read still
uses the explicitly authorized bounded custom IO; it never bypasses that owner.

## Saving from playback

R1 in ordinary live and file IPTV playback now includes Save channel / Remove
from saved. This is independent of Square's favourite toggle. Saving retains
only already-confirmed quality data. It does not restart playback; a failed
write preserves membership and produces a notice. The root drawer holds at most
seven rows, including decoder/restart actions; compact technical information
and the common hint fit underneath. Host render checks cover both membership
states, live/file playback, all eight themes and Italian/English.

## Current logs and reproduced causes

All nine available app log files were read and preserved in a temporary host
snapshot. The current standard session (156499 bytes, modified 11:39 local)
contains actual playback evidence; the new .1 archive preserves the previous
09:37 startup log. Older October 23/24 profiles are historical evidence only.
The public release includes metadata and aggregate errors, without raw logs,
playlist credentials, signed addresses or core dumps.

The current session contains 28 playback attempts: nine normal returns, four
quality-change returns, two cancellations, eight unsupported-selection errors,
three no-variant errors, one HTTP server error and one DNS error. All eight
unsupported selections are at a Rakuten MediaTailor master, before segments or
decoder input. Two direct master requests reproduce different session paths for
the same five quality descriptors. Exact-ID matching remains preferred; a
rotated path now matches one uniquely supported resolution/bitrate/frame-rate/
audio-group descriptor and uses the freshly received address. Ambiguous matches
are rejected. All five real variants pass this regression.

Three no-variant failures refer to jmp2.uk, with insufficient original channel
identity to prove which individual broadcasts failed. A current public Pluto
entry from the same saved catalogue reproduces the cause class: five relative
signed variant addresses are 2468–2469 characters, exceeding the previous 2048
resource buffer. Internal resource/final-response addresses now allow 4096
bytes, including the shared HTTPS client's effective URL. The user-facing root
URL, library record layout and saved-data schema remain unchanged. Actual Pluto
master preparation now succeeds; this does not assert that all three historic
failures had the same cause. HTTP 500 from Pluto and a failed Amagi DNS lookup
remain service/connection errors, not decoder compatibility errors.

The specifically reported public .m3u URL returns HTTP 200 and a 128-byte HLS
master containing one H.264/AAC 1920×1080 variant. Its media playlist and one
bounded segment were fetched; host ffprobe confirms H.264 video and stereo AAC.
It is now classified as one HLS channel instead of an invalid catalogue.
This is host evidence, not proof of native Vita playback.

The current Peppenamir QR response is HTML from the QR service, without a
supported static destination; its earlier successful 380-channel response is
historical, not a guarantee of the current landing page. Scripts are not
executed to recover arbitrary service destinations. Normal cancellation is
propagated as cancellation rather than an artificial remux error.

## Verification boundaries

New host cases exercise all supported import containers, mixed entries, nested
navigation, local path safety, UTF-16, actual .m3u HLS content, real rotating
Rakuten masters and the long Pluto paths. Actual production import/page logic,
storage and decoder boundaries, R1 membership, all themes and IT/EN are tested
under ASan/UBSan. Production FFmpeg HLS selection/remux/decode also passes with
local-mount addresses under ASan/UBSan and TSan. Existing persistent-quality,
failed-playback rollback, source/session, format-discovery and translation
regressions remain required. Build/package checks and a USB copy do not prove
installation, hardware decoding, touch feel or native firmware rendering.

HLS content distinctions follow [RFC 8216](https://www.rfc-editor.org/rfc/rfc8216.html).
