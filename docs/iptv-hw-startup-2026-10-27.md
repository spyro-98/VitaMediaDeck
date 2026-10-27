# IPTV hardware startup — 27 October 2026

This follow-up corrects reproduced HLS timeline and pinned `h264_vita`
lifecycle defects in the standard 1.4.1 / APP_VER 01.05 VPK. Physical first-frame
acceptance for the reported channel remains pending. No emulator, Vita firmware
or ARM JIT was executed during diagnosis or verification.

## Console evidence

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
These examples inform lifecycle/header review; they are not proof that the
reported channel will produce frames with this correction.

## Physical acceptance

1. Install the standard `VitaMediaDeck-1.4.1.vpk` from Vita Downloads. USB copy is
   delivery only; the assistant does not install or change console preferences.
2. Retry the same IPTV channel with Auto, then explicit HW in R1 if necessary.
   Check first frame, fluid playback, audio sync and stop/reopen. Repeat a known
   working local hardware video and a Jellyfin video.
3. If the first-frame timeout persists, reconnect USB with the new
   `session_standard.txt`. The bounded `vita_h264 init` / `startup no-output`
   messages distinguish initialization, API failure and accepted-but-silent input.
   These corrections do not establish which remaining device-specific condition
   is preventing output until the channel is retried.
4. Verify SW fallback/rollback, suspend/resume and long-running HLS. Respect
   plugin-owned clock readback; this build does not force 444 MHz over PSVshell.
