# Integrated Home, fixed L1 decks and app LTO — 2026-10-24

Release: 1.4.1 / APP_VER 01.05, identity standard, bubble VitaMediaDeck,
log `ux0:data/VitaMediaDeck/session_standard.txt`. Delivered in the existing
standard release VPK, with no additional test-specific package.

## Home composition

One Home surface simultaneously presents a priority resume card, saved server
connections, explicit favourites, recent activity and sources/history actions.
Six independently navigable components have theme-dependent sizes/arrangements.
Ghost uses its left content pane with the right wheel preserved; Aperture reserves
its bottom category rail; Signal reverses the secondary panels; Mosaic stacks
secondary cards beside the main item. Every composition respects mini-player space.
Legacy keeps its native material without 3D; the other themes reuse their GXM
scene/focus/material effects. Incoming metadata never restarts per-item entrances.

Cross opens the selected item; saved servers enter their native browser directly,
with existing authentication, SSH host-key/TLS trust and remembered-password policy.
R1/Square cycles items inside a component. Triangle searches globally. Circle
focuses category navigation; sources/history/search open intentional detail pages
and backing out/R1 returns Home. Ghost's right stick continues to own the wheel.
The shelf offsets/focused identity survive player return. Expired focused media
is retained until navigation rather than silently replacing an imminent Cross.

Only actual online favourites and starred IPTV enter the favourites component:
up to eight entries per Radio/Podcast/IPTV provider (24 preview entries total).
Saved-but-unstarred IPTV does not become a favourite. Removed entries disappear
on worker refresh. Cover publications update both the search and Home snapshots.
The complete collections remain available through their providers.

The dashboard does not launch online discovery/server-wide requests on entry.
Local legacy resume import and favourite reads run on low-priority workers;
configured non-secret connections are snapped once on entry. Drawing only uses
memory, cached artwork and existing bounded thumbnail queues. Credential fields never enter card labels; favourite URLs retain the same private
cache handling described in the library report. Existing registry/journal limits,
15-second checkpoint cadence and local-only playback state are unchanged.

## L1 scope

Legacy, Deck 90 and Aperture are vertical lists with animated viewport scrolling,
arrow controls and touch drag. Ghost Mesh uses a fixed circular layout; Signal,
Relay and Mosaic use fixed grids. All actions, including active player/download,
fit within their viewport; directions navigate their actual positions. Fixed decks
have no vertical camera/drag/arrows, and focus animates directly between rectangles.
The audio-layout subpage retains its existing independent controls.

## LTO coverage

The application's existing Release -O3, Cortex-A9/NEON/hard-float/Thumb toolchain,
section garbage collection and FFmpeg C LTO remain. LTO is now applied to the app,
vita_hw_decoder, vita_sw_decoder, vita_https and vitamediadeck_iptv_source, with
compiler-matched gcc-ar/gcc-ranlib. Standalone package defaults, public decoder ABI
2.0.0, dependency revisions, clocks and copied NV12/P2 remain unchanged.
Disabling the app LTO option still disables the added app-owned compile flags;
the separately built FFmpeg archives retain their own existing build configuration.
No -Ofast/fast-math or unsafe whole-program visibility assumptions are added.

## Validation and physical acceptance

The library suites cover real worker/cache/index concurrency under ASan/UBSan and
TSan, explicit favourites/removal/cache reuse, and actual seven-theme host rendering.
The Home UI fixture renders populated/empty compositions, searches and details;
checks every component's bounds, overlap and spatial reachability, saved-server
selection, in-widget cycling and return from History. L1 suites distinguish
scrolling lists from zero-scroll fixed decks, with IT/EN, motion settings, live
slots, direct touch, spatial reachability and audio-layout isolation.

Current host evidence (synthetic data and local controlled servers):

| Check | Result |
| --- | --- |
| App-owned C LTO objects | 98/98: app 71, HW 7, SW 7, HTTPS 3, IPTV 10 |
| Registry / index / sampler / direct-connection contract | 110 / 632 / 6 / 43 checks under ASan/UBSan and TSan |
| Actual search workers | 197 and 198 scheduler-dependent checks, ASan/UBSan and TSan |
| Integrated Home and search rendering | 585,521 checks; 56 layouts across seven themes |
| Game UI / L1 | 147,956 / 389,011 checks; maximum 419 / 273 geometry primitives per host frame |
| Provider API / authenticated range transport | 239 checks / 48,205 byte checks; local HTTP and rejected redirects |
| Existing provider UI | 219,449 checks, seven themes and two languages |
| Online theme flows / cached navigation | 878,738 / 25,998 checks |
| Theme chrome | 19,611 checks |
| Continuous page motion | 490,280 checks; settings/guide/about, audio, downloads and Home regressions also passed |
| Documentation | 87 Markdown documents, 307 relative links |

The repeated assertions and host geometry budgets are regression evidence, not
Vita benchmarks. Handwritten assembly, SDK libraries and prebuilt modules are
outside the 98-object C coverage figure. Detailed logs and host layouts are in
the standard release bundle's `validation/` directory.

Native Release/LTO build, per-archive LTO inspection, matching archive tools,
SELF equality, SFO/CRC, licensing/TLS audit and source/bundle checksums are required.
These establish build/static/host evidence, not physical Vita timing or battery.

On Vita: test all seven Home compositions with real resume/favourites/servers,
R1/Square/touch and player return; open each saved protocol with remembered
credentials ON/OFF and declined/accepted trust; search during playback; exercise
L1 with player/download simultaneously, every fixed-grid action and the scrolling
lists; test HW/SW playback, MP4/MKV seeking, subtitles/audio tracks, suspend/resume
and malformed/absent media. Compare responsiveness, frame stability and battery
before attributing gains to LTO; the new UI changes also affect total binary size.

Repeat the native LTO object audit with:

```sh
python3 tools/audit-app-lto.py --build /path/to/cmake-build \
  --readelf "$VITASDK/bin/arm-vita-eabi-readelf"
```

It reads the actual linked app objects and static archive members, checks GCC LTO
sections and compiler-matched archivers, and fails on incomplete app-owned coverage.
The helper currently targets CMake's Makefiles generator used for this release.
