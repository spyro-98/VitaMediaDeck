# Playback performance audit

This audit records the causes and verification boundaries for the August 2026
startup, seek, track-switch, subtitle, mini-player, and cover-preview work. It
separates code/package validation from measurements that still require a real
PS Vita and representative local and network media.

## Reproduced reference file

The local 2.1 GiB `Suzume` VitaMediaDeck Matroska sample used during the audit is
7,285 seconds long and contains one 960x544 H.264 video stream, three stereo AAC
tracks, four supported text-subtitle tracks, seven font/other attachments, and
one 480x272 attached MJPEG picture. The attached JPEG is syntactically valid but
only 987 bytes and visually black. A representative decoded frame is non-black.
This proved that the old cover path could complete successfully while producing
an image indistinguishable from the pitch-black OLED background.

Host FFmpeg opens this indexed file in roughly 0.14 seconds and seeks/decodes a
frame at 60 seconds in roughly 0.05 seconds on the audit Mac. Those values are
not Vita performance claims; they show that this sample has a usable index and
that the previous multi-second Vita seek was not inherently required by its
container size.

## Confirmed causes and corrections

| Area | Confirmed cause | Correction |
| --- | --- | --- |
| Long-video open | A decoder-owned five-second FFmpeg probe deadline returned `AVERROR_EXIT`, which AUTO mode could misclassify as a user cancellation. A zero-frame EOF/error could also be published as a successful open. | Internal deadlines are distinct from the caller cancel token; unusable zero-frame startup is an error and AUTO fallback remains available. |
| Startup latency | The app opened a third synchronous demux cursor only to rediscover tracks already visible in the playback demux. Indexed inputs could also probe unrelated streams. | Track metadata is snapshotted from the open video demux. Readiness checks are limited to streams required by the operation and header/probe work is wall-bounded. |
| Random seek | The old decoder discarded the backward random-access packet selected by FFmpeg and scanned toward a later keyframe. Every seek also destroyed, reallocated, and cleared the large CDRAM presentation pools. | The decoder consumes the backward keyframe, suppresses only decoded preroll pictures, retains compatible frame pools, reuses both demux cursors, and releases playback after one decoded frame or the bounded gate. |
| Audio switch | Track changes reopened/reprobed the source and UI cancellation could affect the live session. A failed replacement could also close the player even after the old audio was restored. Known multichannel AAC tracks were advertised despite the Vita AAC path being mono/stereo. | Audio changes seek the existing audio cursor by its stable stream index. A cooperative worker stop preserves a healthy remote cursor and destructive abort is only the bounded fallback. Empty/short track tails and readiness have explicit bounds; rollback is nonfatal, and known unsupported multichannel AAC is excluded. |
| Subtitle switch | First activation synchronously opened/probed/seeked another cursor behind a modal screen. Sparse SubRip read-ahead could scan far ahead, serial cancellation could leave a poisoned cursor, and unrelated packet timestamps could stop the queue. | One low-priority worker owns serial requests. The R panel stays interactive, a five-second watchdog makes pending work retryable, and a short cooperative grace reuses healthy cursors while aborted cursors reopen. Only selected/reference timelines advance the five-second horizon, and overlapping cues cannot permanently block the queue. |
| Cover preview | Black attached artwork was accepted and cached; decode handled FFmpeg send/receive back-pressure incorrectly; low packet limits could starve video in attachment-heavy files. Separately, `suspend()` could leave the global cancel flag set while idle, causing every request after a scene change to fail before opening. | Cache v3 rejects black/near-uniform RGB565 data, drains/retries FFmpeg packets correctly, tolerates many non-video packets, salvages usable probe results, and generates a bounded representative frame when artwork is unusable. Resume clears idle cancellation; UI publication precedes cache writes; GPU LRU eviction and playback CDRAM release happen before new allocations. |
| Local mini-player | Decoder frames were not marked as GPU-owned, so a background seek or teardown could race the last draw. Worker-side decoder joins could also wait on vita2d while the UI rendered its loading scene. | Mini-player draws retain explicit GPU ownership until the next render-complete boundary. Background operations start only after that boundary; seek teardown uses the caller-confirmed fence. |

## Runtime diagnostics

The text log records independent timing for factory open, container header,
stream probe, thread join, demux seek, decoder restart, first frame, and total
startup/seek/audio-switch latency. Thumbnail logs include cache/embedded/frame
origin, elapsed time, black-cover rejection, preemption, and errors. Subtitle
logs include request serial, open time, seek time, retry/failure state, and
selected stream.

These stage timings must be collected on hardware before changing limits. A
large file by itself should not make indexed open or seek linear; a slow stage
should identify transport, header/probe, demux seek, decoder initialization, or
first-frame preroll separately.

## Deliberate remaining limit

Video, selected audio, requested subtitles, and cover extraction still use
independent seekable cursors. This isolates ownership and makes in-place seek and
track changes reliable, but an interleaved remote movie can be read by more than
one demuxer. The five-second subtitle horizon limits each burst; it does not
eliminate duplicated steady-state payload. A future shared packet dispatcher
with bounded per-consumer queues is the architectural route to one network/file
read path. It is intentionally not mixed into this corrective change because it
would replace the decoder concurrency contract and requires its own on-device
stress campaign.

Network responsiveness is also bounded by protocol requests, not guaranteed to
be instantaneous. WebDAV, SFTP, and SMB use finite transport deadlines and
operation cancellation. SFTP/SMB DNS uses joined native Vita resolver jobs;
SSH and SMB setup is advanced in short service slices so Circle and superseding
operations can stop it. DNS and server latency remain external inputs.

## Hardware validation matrix

For each HW H.264 and SW FFmpeg backend, exercise a short and two-hour local
Matroska plus equivalent WebDAV, SFTP, and SMB sources:

1. Cold open at zero and at a saved position; record all stage timings.
2. Seek backward/forward by 10 seconds and to 10%, 50%, and 90%; repeat rapidly.
3. Switch every playable AAC track while playing, paused, muted, and under slow
   network conditions; cancel one switch with Circle.
4. Select SubRip, ASS/sign, Off, and another track repeatedly; navigate home
   while a request is pending; retry a timed-out request.
5. Minimize during playback, draw live video, seek in the mini-player, restore
   fullscreen from its title/current grid cell, and stop during a frame update.
6. Verify sidecar, valid attached, black-attached fallback, and no-artwork frame
   covers after cache invalidation and after relaunch.
7. Confirm no leaked stream handles, threads, decoder surfaces, CDRAM blocks, or
   audio ports after repeated open/seek/switch/minimize/close cycles.

Passing the build, SELF/VPK integrity checks, and host fixture checks proves the
artifacts are internally consistent. It does not replace this physical Vita
measurement.
