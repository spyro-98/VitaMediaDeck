# Controls clarification — 23 October 2026

Follow-up to standard 1.4.1 / APP_VER 01.05, commit 46ba0db. Application version
and decoder settings remain unchanged. The verification VPK uses test ID
`controls-1023`, bubble `VMD controls-1023`, and
`ux0:data/VitaMediaDeck/session_controls-1023.txt`. The frozen original standard
1.4.1 artifact is retained separately; use the follow-up VPK for the new guide.

## Audit and corrections

The dispatch code, localized guide, on-screen labels, README and external control
reference were compared. Runtime key routing is unchanged. The reference also makes the existing browser
mini-player ownership explicit: Start restores it and Select is reserved for lock.

- The seven guide chapters now cover all themes, Navidrome/Immich, context menus,
  QR, destination/transfer controls, player mappings and image controls. The Home
  chapter prepends the actual theme and its navigation hint. The Player chapter
  prepends the saved panel/seek mapping, including the swapped option.
- Guide footer names what Up/Down, Right/X, Left, Circle and L1 actually do for
  index/text focus. Removed the former R1 label, which was not a guide action.
- New-theme Home footer hints follow section/results focus; Legacy online tabs
  also expose their focused-tab hint. Ghost names the right wheel
  and left list independently; Aperture explains its bottom tabs; Deck 90/Relay
  explain entering/returning to the side menu. Mosaic explains that Left/Right
  selects covers while Down/Up changes pages. Footer width spans the available
  rail and reserves the position-counter area.
- IPTV Triangle means Search in lists; All/Favourites remains an R1 action.
  The form still uses Triangle to choose format. Episode rows expose Start
  download and Square description. The guide distinguishes country/catalogue
  actions and shows short Select details separately from held input lock.
- Player panel tips describe row/choice/apply/close. HUD hints use two short
  lines so Start/ECO and held Select remain visible with Deck 90's mono font.
  IPTV captions respect the saved panel/seek mapping. Music options name row
  selection and change/close controls. Image hints fit both font families without
  truncating Start reset or Circle exit.
- Common button badges use X/O for Cross/Circle and translated Triangle/Square
  names. The Home-order screen says Cancel rather than Back when discarding edits.
- README and mds/CONTROLS.md now describe the same seven theme routes, Mosaic
  paging and player mapping. Removed obsolete instant Select lock, matrix layouts,
  and the blanket claim that no online catalogue discovery exists.

## Validation

Production guide/content formatting is exercised with all seven themes, IT/EN,
normal/swapped player mapping and mini-player occupancy. Tiny content buffers
fail safely. Hints are measured using the actual Inter/IBM Plex Mono faces;
menu/HUD/guide rails must fit their reserved widths. Scripted guide navigation
enters Home text, scrolls it, returns to the index and exits.

Shared UI/chrome, theme-flow, player-feedback and Home-order regressions check
input ownership and button-label integration. Native Release/LTO and VPK audit
remain required. The bundle `build/controls-20261023/` contains exact final logs,
metadata, source revision and hashes; its manifest identifies checks rerun.

Host references are not Vita captures. Actual pad/stick feel, CJK fallback,
OLED readability, HUD placement over video and real controller hold timing
still need console acceptance. No new decoder performance or battery result
is inferred. The broader checklist remains [VALIDATION.md](VALIDATION.md).
