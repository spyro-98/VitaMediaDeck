# IPTV hardware startup — 27 October 2026

This follow-up corrects reproduced HLS timeline and pinned `h264_vita`
lifecycle defects in the standard 1.4.1 / APP_VER 01.05 VPK. On 27 October 2026,
the user confirmed excellent IPTV HW playback on a physical Vita after installing
the corrected build. The assistant's host checks executed no emulator, Vita
firmware or ARM JIT.

## User-confirmed console result

The user reports that IPTV with the hardware decoder now works excellently,
including many sources described as 1080p. The first-frame startup problem is
therefore resolved for the IPTV sources they tested. This is physical-device
feedback from the user, separate from the host tests and the older failing log
below. It does not independently measure selected-rendition dimensions, FPS,
bitrate, actual clocks, battery use or long-session duration; documented input
and automatic rendition limits are unchanged.

Tested standard VPK: **1.4.1 / APP_VER 01.05**, source commit
`5419ffe27f93913bb7e067199b2e8c938ad9a8cb`, 6,022,158 bytes, SHA-256
`774278ef33d8b627d60319fb9c43addafae2382b1dbe93b23e69e89d73aab9cf`.
The VPK was copied and read-back verified in Vita Downloads before the user's
successful test. This documentation update does not rebuild or replace it.

## Previous failing console evidence

The user-provided `session_standard.txt` has SHA-256
`f651d62b337fd23ba22b2b0612775cb192b282405be70652690aa9ef629025b8`.
It records successful local 960×544 hardware playback at an actual 333 MHz,
then IPTV 1280×720 hardware startup with zero output frames: 246 packets before
the initial application timeout, and 745 / 795 packets in later attempts.
The source buffer continues filling. This is an application first-frame timeout,
not an identified numeric SceVideodec rejection. Software fallback produces about
9.9 fps for the reported 25 fps channel. A requested SW 444 MHz reads back as
333 MHz; the cooperative clock policy yields to external control rather than
trying to override it. No plugin profile is modified.

The authorized same-origin probe identifies H.264 Main level 3.1, 1280×720/25,
8-bit 4:2:0 progressive, one reference and no B-frames, with in-band SPS/PPS and
IDR access units. Those parameters alone do not prove Vita HW compatibility.
Private URLs, credentials, raw console logs and provider media are not included
in Git or the release evidence.

## Corrections and scope

- `hls_source.c` no longer subtracts a fixed 17-frame decode lead from every
  H.264 stream. MPEG-TS timestamps are preserved; length-prefixed CMAF uses only
  its reported reorder depth, with explicit bounds. Presentation timestamps and
  the shared audio/video epoch retain their existing relationship. This applies
  to sources routed through the IPTV HLS remux, not the separate DASH remux.
- With no initial extradata, the pinned `h264_vita` codec can initialize inside
  in-band SPS/PPS parsing, then immediately delete and recreate that decoder in
  the deferred-init branch for the same first access unit. Successful validated
  initialization now consumes the pending obligation once.
- Buffer cleanup clears each ownership bit before release. Reinitialization
  followed by a partial allocation failure can no longer release stale resources
  a second time. Cleanup is idempotent.
- Bounded numeric startup diagnostics record initialization parameters and
  accepted access units with no output at counts 1, 2 and powers of two through
  256. Steady-state playback adds no per-frame messages. No stream URL is logged
  by this addition.

The backend lifecycle fixes apply to every source using the app's shared
`h264_vita`, including local files, Jellyfin and network files. Jellyfin currently
opens a static authenticated range stream; it does not pass through this HLS
remux. Audio-only radio/podcasts do not use the video backend. No public decoder
ABI, firmware imports, clock policy, source codec support or theme is changed.
No speculative SPS/VUI rewriting is applied.

## Verification

| Check | Result and limit |
| --- | --- |
| Actual production HLS session/remux against the authorized channel | Before: artificial PTS−DTS = 61,200 ticks / 680 ms at 90 kHz. After: PTS = DTS, matching the source. Both inspections find 100 video packets, 6 SPS-bearing packets, 6 PPS-bearing packets, 2 IDRs and no discard flags. Zero initial extradata reproduces the deferred codec path. Host networking and packet inspection, not Vita decoding. |
| Actual pinned codec C with real FFmpeg SPS/PPS parser, mocked Vita memory/decoder APIs | 18 ASan/UBSan cases pass: in-band/global/new extradata, repeated headers, resolution change, seven failure points at startup and during resize, repeated close. One init/create and zero deletion before first AU; zero duplicate releases. |
| Reverse-patched negative control | Reproduces two init/create calls and one deletion before first AU, plus up to five duplicate releases on failed resize. Returns normal exit 1; no firmware or crash. |
| Production HLS pipeline | Eleven 12-second synthetic fixtures pass under ASan/UBSan and ThreadSanitizer: TS/fMP4, low-delay streams with packet PTS=DTS assertions, B-frames, signed composition offsets, separate A/V, ranges, encryption/key rotation. Selection/fuzz and 89 lifecycle/cancellation checks also pass. CPU FFmpeg decode only. |
| DASH regression | MPD/security, real CPU remux/decode, shifted epochs, 32-chunk backpressure, live refresh and 110 lifecycle checks pass under ASan/UBSan and ThreadSanitizer. This is separate from the HLS timestamp correction. |
| Shared decoder wrappers and clock policy | HW-adapter, SW and planar host suites plus cooperative HW333/SW444 policy checks pass. Hardware/power boundaries are substituted. |
| Existing MP4/MKV playback helpers | Subtitle rearming, duration, bidirectional seek, logging and player adapter checks pass under ASan/UBSan. No on-device seek timing claim. |

The exact patched ARM FFmpeg dependency is rebuilt with the existing pinned
compiler/profile, including LTO. The standard build then runs the release,
license/source, native import and app LTO audits. Delivery manifests record the
actual resulting hashes and logs; pre-build checks are not device acceptance.

Patch: `tools/patches/ffmpeg-vita-lifecycle.patch`, SHA-256
`845f80ea710b9092f73d9b3e87caf04c138ff3d99b00012f61f1b955973144a1`.
The builder applies it after the pinned wiliwili and custom-I/O patches and ships
the exact patched source archive. Release audit compares the installed patch with
source and its dependency-lock hash. The backend harness is documented in
[its README](../tests/h264_vita_lifecycle/README.md).

Reference comparisons: [VitaSDK decoder API](https://docs.vitasdk.org/group__SceVideoDecUser.html),
[Moonlight Vita decoder](https://github.com/xyzz/vita-moonlight/blob/vita/src/video/vita.c)
and its [SPS handling](https://github.com/xyzz/vita-moonlight/blob/vita/libgamestream/sps.c).
These examples inform lifecycle/header review. The physical playback confirmation
comes from the user's subsequent test recorded above, not from the examples or
host mocks.

## Acceptance scope and additional coverage

IPTV HW startup and playback are user-confirmed for the tested sources. The
broader regression checklist remains separate: repeated stop/reopen, long-running
HLS, SW fallback/rollback, suspend/resume, and a fresh Jellyfin/local-file
comparison were not individually reported in this latest confirmation. The
backend fix is shared with those HW source paths, but this result does not claim
they were all retested. Existing clock ownership rules still respect PSVshell.

If a different source fails, the standard log's bounded `vita_h264 init` and
`startup no-output` messages remain available for diagnosis. Do not publish raw
private logs or authenticated source URLs.
