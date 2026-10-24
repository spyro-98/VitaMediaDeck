# Validation guide — VitaMediaDeck 1.4.1

This is the current suite/acceptance index. Historical reports keep their own
build IDs and dates; see [documentation index](README.md).

## Evidence levels

| Evidence | Establishes | Does not establish |
| --- | --- | --- |
| Source inspection | paths, bounds, ownership and selected options | successful device interaction |
| Host tests / reference PNGs | logic, geometry, cancellation and state transitions with substituted platforms | Vita camera, IME, GXM timing or power |
| Native Release/LTO + audit | compiler/linker integration, archive/SELF/SFO/font/provenance checks | installation, playback, account login or real display contrast |
| Read-back SHA-256 on mounted Vita | delivered bytes match the artifact | installation or execution |
| User observations and PlayerFix logs | the files, clocks and actions actually tested | all media, themes, providers or newer builds |

## Host prerequisites and suites

Run from the app repository with clang, bash, Python 3, pkg-config, FreeType and
libpng; media suites also need ffmpeg CLI, configured pinned FFmpeg 6 host
archives, libjpeg/libwebp and optional Jansson/crypto test prefixes. macOS scripts
use `xcrun --show-sdk-path` for SDK headers. Configure required `VMD_HOST_*`
variables from actual dependency locations, never Vita archives as host libraries.
Suite-specific README files identify substitutions and environment variables.

```sh
bash tests/ui_crossfade/run-host-tests.sh
bash tests/ui_crossfade/run-scenes.sh
bash tests/ui_crossfade/run-game-ui.sh
bash tests/ui_crossfade/run-theme-flows.sh
bash tests/ui_crossfade/run-motion.sh
bash tests/ui_chrome/run-host-tests.sh
bash tests/ui_coverage/run-host-tests.sh
bash tests/home_tabs/run-host-tests.sh

VMD_HOST_FFMPEG=/path/to/configured/ffmpeg6 bash tests/decoder/run-host-tests.sh
VMD_HOST_FFMPEG=/path/to/configured/ffmpeg6 bash tests/playback_seek/run-host-tests.sh
VMD_HOST_FFMPEG=/path/to/configured/ffmpeg6 bash tests/player_feedback/run-host-tests.sh
```

| Area | Suite / guide |
| --- | --- |
| Page physics, focus, preferences, audio, settings, guides, dialogs, loading | [crossfade](../tests/ui_crossfade/README.md) |
| Actual chrome/L1/status; per-theme order | `tests/ui_chrome/run-host-tests.sh`, [Home order](../tests/home_tabs/README.md) |
| QR, native editor backdrop, logs | [utility coverage](../tests/ui_coverage/README.md) |
| Image pan/zoom/rotation | [image viewer](../tests/image_viewer/README.md) |
| Independent cursors, MP4 durations, subtitle rearming, build logs | [playback seek](../tests/playback_seek/README.md) |
| Seek preview, mini-player and thumbnail lifetimes | [player feedback](../tests/player_feedback/README.md) |
| Radio/audio protocol and caches | [online audio](../tests/online_audio/README.md), [metadata](../tests/podcast_metadata/README.md) |
| Lazy navigation and stale replies | [online navigation](../tests/online_navigation/README.md) |
| Navidrome/Immich adapters | [personal media](../tests/personal_media/README.md) |
| IPTV/HLS/DASH adapters | [IPTV](../tests/iptv/README.md) |

Some transport tests bind a local server and need local-network/socket permission.
Fixtures are synthetic; do not add credentials, copyrighted media or raw console
logs to Git. Host HW fixtures substitute the hardware decoder; passing does not
mean the Mac exercised SceVideodec.

Podcast resume regressions: `bash tests/podcast_resume/run-host-tests.sh`
checks the production persistence/worker, restart and failure recovery, write
budget, plus the production MP3 function with real CBR/VBR decoding. See the
[suite boundaries](../tests/podcast_resume/README.md) and
[console checklist](podcast-resume-2026-10-24.md).

`run-game-ui.sh` also runs the scrolling L1 fixture across all seven themes,
IT/EN, reduced motion and player/download combinations. See
[scrolling coverage and console checks](l1-scroll-2026-10-24.md).

## Native standard release

Use the [toolchain guide](../mds/setup/TOOLCHAIN_SETUP.md) and
[1.4.1 commands](releases/1.4.1.md). Check human version 1.4.1, APP_VER 01.05,
TITLE_ID VMDK00001, bubble VitaMediaDeck, ID standard and session_standard.txt.
Check all default/experimental cache flags explicitly. Run `git diff --check`,
VPK CRC/SELF equality, metadata/version checks, SHA-256 and `tools/release-audit.sh`.
Pin the current decoder/HTTPS Git revisions after their intentional commits.

## Physical-console checklist

1. Update installation, restart, retained settings/services and seven theme choices.
2. Rapid forward/backward page and menu changes: no per-item replay/flicker,
   visible focus borders and touch matches the moving item. Test reduced motion.
3. All Settings tabs, Home ordering save/cancel, guides, About/Logs, file actions,
   delete confirmation, destination selection and mkdir, with/without mini-player.
4. QR from Network and IPTV: real HTTP/HTTPS QR, unsupported payload, Circle and
   touch cancellation; return to the correct page. No camera overlay occlusion.
5. Native keyboard over URL/search/password prompts, caret, masking and dismissal.
6. Actual Navidrome/Immich/Jellyfin/WebDAV/SFTP/SMB authentication, art and navigation.
7. Radio/podcast cached data then refresh; empty/offline/stale state and thumbnail reuse.
8. Download progress, pause/resume/background/cancel, success and error contrast.
9. Local/remote MP4/MKV total duration, seeks both directions, subtitles after seek,
   audio-track switching, startup, frozen-frame seeking and mini-player restoration.
10. Playback/frame pacing, temperature and battery with controlled media and actual
    clock readback. Test SW 720p30 separately; no guarantee at CPU 333 MHz.

## Existing physical evidence

The user reports the previously described MP4/subtitle bugs resolved. PlayerFix
logs contain 39 successful seeks and 22 subtitle applications; HW 720p30 sample
29.8 fps at API-reported CPU 333, BUS/GPU 222 and XBAR 111 MHz. This is not proof
of a complete 333/166/166 profile or 720p60. SW 720p30 starts can time out at 333;
Auto recovers HW. Planar throughput/quality/energy superiority is unproven.
See [the actual record](console-validation-playerfix-2026-10-23.md).

The current 1.4.1 release integrates the accepted work; no fresh physical run,
battery measurement or successful authenticated-provider matrix is inferred
from compilation, source review, an emulator or host reference pictures.

## Controls follow-up

The post-1.4.1 [controls audit](controls-guide-2026-10-23.md) uses a distinct
controls-1023 test identity. Its guide tests add actual-theme/current-mapping
prefixes, hint-width limits, safe buffer failure and index/text navigation.
The repository README and mds/CONTROLS.md use the same verified theme routes.
The original archived standard VPK is not silently replaced by that test build.

## Unified library/search follow-up (library-1024)

Run `bash tests/library/run-host-tests.sh` (ASan/UBSan and TSan),
`bash tests/library/run-ui.sh` (actual seven-theme Home on the host renderer),
and the existing personal-media, podcast-resume, UI/chrome/motion suites.
The provider HTTP fixtures now check encoded Jellyfin search and canceled
Jellyfin/Immich requests. See the [implementation and Vita/server acceptance
checklist](unified-library-2026-10-24.md). Host counts, synthetic layouts,
native Release/LTO and VPK checks do not establish real account/device behavior.


The `search-1024` follow-up refreshes the library suites with the production local
index reader, worker-only query ownership, late-cover publications, 42 host layouts
and animated/Reduce Motion activity in all seven themes. It uses the same manual
Vita/server checklist; host budgets do not measure console FPS or battery usage.
