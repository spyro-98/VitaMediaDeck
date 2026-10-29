# IPTV errors, preferences and diagnostics — 29 October 2026

Saved channels offer Rename/Remove in R1 both in saved-channel collections and
when a previously saved channel is selected inside an open playlist. Removing a
saved reference retains the source playlist/channel entry and clears its local
star/quality badge. Playlist management keeps its existing focused-pane rules.
Remove is grouped directly after the favourite action in Collection; the old
special sorting rule forced it to the very end of the drawer, outside its first
viewport. Removal retains the existing Cancel-first confirmation.
The real menu uses 16 action slots; legacy host fixtures now use that same size.

Decoder creation/open success alone no longer confirms a video preference.
The ordinary player loop requires at least three seconds of advancing presented
video, multiple real frames and a recent presentation, without pause/error/EOF.
This is startup validation, not the optional R1 multi-resolution smoothness
benchmark. Once confirmed, the independent persistent choice is committed before
player exit. A later playback failure rolls that transaction back to the prior
choice, or removes the new entry if there was no prior choice. Favourite quality
capture uses confirmed data rather than the producer's untested active variant.
Changed favourite preferences are restored with the playback transaction.

An explicit quality switch that fails reopens the previous selection with a
fresh session after teardown; the notice retains the actual error reason. If an
already saved manual quality fails on entry, a Cancel-first dialog offers one
explicit retry with source-default quality. It retains the saved choice until
the new playback is confirmed; failed recovery/cancellation does not replace it.
DNS/TLS/access/rate/server failures do not trigger this resolution recovery.

Channel-catalogue HTTP/parser limits are now 20 MiB, independent from playback
manifest/segment limits. Oversized bodies and overlong individual lines have
different error codes/messages. The existing 512-channel display cap and its
truncation feedback remain; increasing the download limit does not allocate an
unbounded catalogue. Memory exhaustion is distinguished from buffer limits and
unsupported playback; messages suggest a relevant next action.

The fixed VitaMediaDeck breadcrumb wordmark has 18 extra pixels of native text
space, including its final glyph, to account for differing PAF and packaged-font
advances. Native routing checks the actual box passed to PAF in every theme;
host preview pixels still substitute firmware text and do not certify Vita
appearance.

## What was actually available on the console

At inspection on 29 October, session_standard.txt had 27 startup-only records,
modified at 09:37 local time. Other app session logs and core dumps predate the
reported night. The installed eboot matched the previous delivered build.
The old log_save replaced the prior session; there was no archived night
playback log to reconstruct. Therefore no per-channel/provider root cause is
claimed for those lost failures, and no speculative decoder compatibility patch
was made on that basis. Available files were preserved in a temporary local
snapshot before any new package copy; the release contains a metadata-only
inspection report without URLs, credentials or memory dumps.

Logging now retains 16 prior sessions per log path (about 4 MiB at the current
256 KiB maximum), rotates only once per process/path and adds a UTC session
header that survives RAM tail trimming. When the RAM buffer fills it discards complete oldest records and keeps
new failures instead of silently stopping. Existing disk-logging preferences are
respected. No existing console preferences/playlists/logs are edited during
build delivery; retention starts when the user runs the new binary.

## Verification and limits

Actual play_link/dialog/store tests cover failed changes before and after
confirmation, rollback with and without a previous choice, explicit Automatic
recovery, failed recovery, Cancel and non-retryable TLS errors, across eight
themes and IT/EN. Actual player-helper tests exercise presented-frame/time
confirmation and reject opened-only, paused, errored and frozen playback.
Storage tests cover per-channel removal, stable identities, unrelated records,
failed writes and fresh-process SIGKILL restoration after confirmed playback.
Catalogue tests parse a complete 20 MiB body, reject one byte over the boundary
and distinguish an overlong entry. Log tests cover concurrent producers,
retention, repeated saves, rotation failure and disabled logging.

Decoder/source/session regressions cover fresh opens, release ownership,
cancellation, buffer/error classifications and actual pinned H264 lifecycle
failure cleanup with mocked hardware. These are host ASan/UBSan/TSan checks;
they do not establish actual broadcaster availability or physical Vita playback
at each resolution. Those night failures remain unverified without their logs
or a new console reproduction.
