# Home, media controls and decoder clocks — 27 October 2026

Integrated in the standard 1.4.1 / APP_VER 01.05 VPK, bubble VitaMediaDeck,
log session_standard.txt. No separate test VPK or installation by the assistant.

## Navigation and presentation

- Home keeps all category tabs. Its shared repeat state prevents a held Right
  from advancing twice when leaving Home. The first Down still enters item one.
- × enters populated Favourites, Recently played and Saved connections;
  directional input selects their items and a second × opens the selection.
  ○ returns to components before returning to tabs. Visible items remain direct
  touch targets. □ / R1 also cycle items without replacing Home with a list.
- Home adds a magnifier + △ beside the app icon, removes its footer legend,
  expands the VMD hero and shifts Favourites down. Recent activity has another
  row when enough items exist. Empty components still yield their space.
- Hero transport reads □ index/count R1. Controller hints use □ ○ × △;
  audio/video/mini-player skip buttons use hollow ◁ ▷.
- The app search editor uses explicit light text. Changing native strings
  reapplies layout, alignment and clipping attributes. Timing, volume, audio
  status, search input and ECO labels use the existing frame-synchronous text
  path; native PAF button geometry remains. ECO submits three transition frames
  before idle so the preceding retained native buffer message can clear.
- Country flags have a separate 24-slot 64×64 RGB565 texture cache (at most
  192 KiB extra), independent of the unchanged 12 media texture slots.
- Radio/podcast favourites toggle with an inline star and no success dialog.
  Countries can be saved independently for radio and podcast; a local validated
  country shortcut opens its catalogue. It is never passed to a media/network
  URL opener or published as a playable global-search/Home result.
- R1 switches Legacy/VMD cover catalogues between one and two rows. Other themes
  retain their distinct list/grid geometry. Catalogue touch swipes move through
  items; the shared vertical R1 action menu supports touch dragging and keeps
  actions beyond its eight visible rows reachable.

## CPU pairing and plugin ownership

Settings → System → Clock control defaults to **Auto by decoder**. Hardware
CPU defaults to **333 MHz**, software CPU to **444 MHz**. Each target can be
changed independently to 222, 333 or 444 MHz. The active attempt selects the
CPU before opening HW or SW, including AUTO fallback and player R1 changes.
The independent background/mini-player decoder also honors the saved backend
preference and owns a guard through decoder destruction, including failed opens.
BUS/GPU/XBAR remain 222/222/111 MHz in the standard build. Audio-only playback
and normal UI do not acquire a video clock guard.

The new checksummed V4 preferences append one word; V1/V2/V3 layouts/checksums
remain readable, migrate to the new pair and preserve all existing flags.
Writes use the existing temporary/backup commit, outside playback workers.
The worker callback uses a settings snapshot, with no persistence per frame.

**PSVshell manual** means zero clock setters, including restoration. In Auto,
only public ScePower APIs are used. PSVshell's manual per-domain hooks retain
priority even when the application's requested target matches a manual value.
Every changed target is read back. A failure, ignored request or later external
change releases ownership of that domain for the rest of this video session;
the app neither retries nor restores over it. An existing CPU above 444 MHz is
preserved without a write. Otherwise only an app-changed, still-matching value
is restored on exit. No plugin files, profiles, hooks or kernel memory are read
or changed. This is cooperative API ownership, not an invented plugin-detection
API or a guarantee about every third-party overclock plugin.

Reference implementation: [PSVshell power hooks](https://github.com/Electry/PSVshell/blob/master/src/main.c),
[manual target selection](https://github.com/Electry/PSVshell/blob/master/src/oc.c)
and [user guide](https://github.com/Electry/PSVshell/blob/master/README.md).

## IPTV evidence and current limit

The supplied console log SHA-256 is
`26c0931a93dc0a3e93718c49b0f3d1477856e4d0317e58a187a0b9b6f435ad64`.
Raw logs, saved URLs and provider credentials are not part of this release.
AUTO attempted H264_VITA_HW at 1280×720, consumed 247/325 packets, produced
zero frames and timed out at the first-frame gate. The subsequent FFMPEG_CPU
session ran around 9–10 fps / 0.36–0.41× wall time. The old app requested 444 MHz
but read back 333 MHz with successful setters: it cannot assume a request was
applied. Buffered data progressed, so a frozen loading label alone was misleading.

With explicit user authorization, a direct probe of the same saved origin
found Main profile, level 3.1, 1280×720 at 25 fps, progressive 8-bit 4:2:0,
Annex B, one reference picture, POC type 2, and SPS/PPS/IDR access units. These
facts do not identify the actual SceVideodec rejection. The public VitaSDK,
Moonlight and ViTube paths were compared; no unsupported claim that a larger
CPU clock, longer timeout or speculative SPS rewrite fixes HW is made.

This build exposes real buffer segments/bytes/elapsed time and the HW/SW phase
while opening, keeps cancellation, permits session HW/SW selection in R1 while
AUTO remains saved globally, and returns to the previous backend with an
explicit notice if the requested one fails. The pinned h264_vita numeric error
messages now enter the existing app log. Live changes reopen at position zero
rather than passing a forbidden seek target. **Successful HW decoding of the
reported IPTV stream is still unverified and the underlying timeout is open.**

## Verification boundary and Vita acceptance

Host suites cover production Home/input, eight themes, native adapter state,
flag/preview lifecycle, online persistence/navigation, search and preferences.
CPU guard tests model all 16 per-domain manual-hook combinations, rejected or
ignored setters, external changes, custom pairs, AUTO transitions and exit.
Full affected production files compile with the actual ARM Release/LTO/Werror
flags. Final VPK/import/LTO/source/license audits are retained beside the release.
Host textures, CPU mocks and API boundaries are not Sony rendering, power,
real H.264 HW decoding or physical Vita timing measurements. No emulator,
firmware execution or ARM JIT was used.

On Vita:

1. Enter each populated Home collection, select a later item and open it;
   check ○ exit, rapid Home → Video → Music and △/magnifier search text.
2. Check hero/footer proportions with/without mini-player and three recent
   items; verify changing timing/volume, touch HUD hide/show and ECO text.
3. Save/remove an Italian country shortcut in both radio and podcasts; verify
   independence after restart, inline stars, stable flags, R1 two rows and touch.
4. In Auto clocks play known working HW content, then explicit SW content:
   verify requested/actual 333 and 444 MHz in the log. Change each target in
   Settings and check persistence; test a manual PSVshell profile and a manual
   change during playback. PSVshell mode must leave every domain untouched.
5. Retry the reported IPTV channel, observe HW/SW startup phase, try R1 HW and
   keep session_standard.txt if it fails. Check other known-working channels.
   That new log is required to distinguish hardware API errors from no-output
   input conditions; the existing log cannot establish that distinction.
