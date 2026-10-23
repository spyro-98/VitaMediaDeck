# Theme coverage — 23 October 2026

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

Scope: all seven interface themes, including Legacy; this working tree includes
the continuous page/menu motion work documented in `theme-motion-2026-10-23.md`.
Base release: 1.4.0 / APP_VER 01.04. Test build ID: `theme-cover-1023`.

## Corrections

- QR presentation separated from camera/quirc ownership. Theme-specific gutter
  brackets, materials, a peripheral 3D mark in non-Legacy themes, shared status
  header, translated IT/EN states/instructions, wrap-safe status text and a
  visible startup view. No decoration is drawn over the live crop. Display and
  decoder share constants: central 480x480 crop at x=80, displayed 444x444.
  Touch cancellation uses the same geometry as the rendered button.
- Native text-input backdrop now uses common themed background/materials. The
  keyboard itself remains the system IME, whose keys are owned by Vita OS.
- Initial player loading uses the selected non-Legacy background and cached
  clock/battery/network indicators, with title width reserved for status. Seek
  loading retains its frozen frame and compact scrim without a decorative
  background pass over the video.
- Error/success badges choose contrasting ink against their actual status
  color. Mini-player close control uses the selected palette and contrasting
  ink instead of the previous fixed burgundy/pink pair.

## Screen inventory and evidence

All drawing entry points under `src/ui` and the video screen were inspected.
Common application pages call `ui_chrome_background`, which dispatches the
selected theme's effects; panels/actions use `ui_panel`/`ui_theme_fx_surface`.
The media HUD uses `ui_theme_media_begin/end` so Signal remains readable over
video/images, without changing the chosen theme or decoded media pixels.

| Screen / nested view | Theme path | Verification in this task |
| --- | --- | --- |
| Home, local video/audio/photos, categories | themed Home + page viewport | source inventory; existing native scene fixtures |
| Local folders/files, empty states, details | chrome + browser scenes + focus | source inventory; native browser geometry/scene fixtures |
| Folder destination / mkdir | chrome + panel + picker viewport | source inventory; prior picker regression retained |
| Network sources, add/edit/remove, direct URL, connection/pinning details | chrome + themed panels/actions | all five production drawing paths inspected |
| Remote folders/files, Navidrome/Immich content | shared remote browser + theme scenes | source inventory, shared browser scene fixtures |
| Navidrome/Immich server selection | chrome + themed panels | source inventory; authenticated runtime requires servers |
| Radio/podcast favourites, catalogues, episodes, offline views, detail pages | common Home/browser or chrome + panels | source inventory, existing Home/audio fixtures |
| IPTV collection, link form, list/channel selection, delete dialog | chrome + panels/context menus | production source and shared UI fixture inspected |
| QR startup/search/candidate/decode/rejection/success | new QR presentation + real common header | 196 host compositions; full live crop checked pixel-by-pixel |
| Settings: all tabs including subtitle preview | chrome + panels, user-selected subtitle style | all tabs rendered in seven themes, IT/EN, with/without mini-player |
| Theme tab ordering | common header + panels | production chrome test including save/cancel/failure rollback |
| Input editor and search field | real themed brand backdrop + native IME | seven themes, IT/EN and stationary reduced-motion editor |
| About / technical specifications | chrome + themed panels | real page rendered in seven themes, IT/EN |
| Session log list and reader | chrome, tabs, themed text/scroll indicator | real drawing rendered in seven themes, IT/EN |
| Categorized controls guide | chrome + themed chapter menu/panel | real page rendered in seven themes, IT/EN |
| Plain/tabbed text: descriptions, notices/licenses | chrome + themed text/panels | real readers executed in seven themes, IT/EN |
| File actions and delete confirmation | themed modal/panels/focus | real pages rendered in seven themes, IT/EN |
| Generic loading, download active/paused, known/unknown size | themed chrome + progress/actions | real draw path rendered in seven themes, IT/EN |
| Success/error/camera unavailable | common themed message screen | real messages rendered in seven themes, IT/EN |
| Audio fullscreen and option panels | media palette + themed audio scenes | production audio fixture in seven palettes; native scene regression |
| Video fullscreen/HUD/options | media palette + video layout/scrim/transport | source inspected; native video geometry/seek lifecycle checks |
| Image viewer/options | media palette + image scene/panels | source inspected; decoder/image pixels intentionally not recolored |
| Mini-player, L1 sections, R1 contextual menus | common palette/panels/glow, continuous menu transforms | source inspected, chrome/L1 tests, mini-player return regression |
| Player startup and seek acquisition | themed startup, media scrim over frozen seek frame | real loading function rendered in all palettes; retained canvas asserted |
| ECO OLED and input lock | intentionally black canvas, themed status | source inspected; real console check remains |

Color exceptions are intentional: decoded images/video/camera frames, user
subtitle colors, cover-art audio color schemes, the subtitle test backdrop,
black media/occlusion/ECO surfaces and Legacy battery semantics. No camera,
network authentication, QR-decoding algorithm or decoder backend was changed.

## Reproduction / limits

Run the utility coverage, shared page, chrome, scene and player-feedback suites;
keep logs and PNG references with the VPK. Host native raster fixtures check
layout and logic. They are not GXM/OS camera/IME captures or measurements of FPS,
power, memory use on console or successful authenticated playback.

Before accepting on console, check QR from both Network and IPTV with a real
HTTP/HTTPS QR, cancellation (Circle/touch), an unsupported QR, then return to
the previous screen. Check the OS keyboard overlay and contrast of the seven
themes on the actual display. The VPK is copied to Downloads, not installed.
