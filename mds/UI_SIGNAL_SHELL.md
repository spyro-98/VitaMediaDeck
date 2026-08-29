# Signal / Shell UI direction

VitaMediaDeck uses a code-native visual system called **Signal / Shell**. It is
an original interpretation of the screen-graphics principles discussed in the
[Pushing Pixels interview with Stylow](https://www.pushing-pixels.org/2019/04/19/the-art-and-craft-of-screen-graphics-interview-with-stylow.html),
not a reproduction of film frames, logos, or interface assets.

## Visual grammar

- Obsidian and carbon planes preserve the OLED's natural black and leave room
  for video, artwork, and multilingual text.
- Signal amber communicates focus and direct action. Cold cyan is reserved for
  machine state, scans, locks, and buffers. Hot white carries primary
  information; green, yellow, and red remain reserved for semantic status.
- Sparse particles, wire connections, scan divisions, and incomplete
  acquisition corners provide the recurring spatial motif.
- A thin command rail, full-height section index, numbered settings banks, and
  asymmetric information panels give each scene a clear operational hierarchy.
- Motion is deliberately slow and deterministic. The Reduce motion preference
  freezes ambient phases and keeps every control usable without animation.
- One vertical memory scan crosses the machine-side telemetry field. It samples
  the warm particle network with cool packet trails instead of turning the
  whole interface into a generic cyan HUD.

## Scene interpretation

- **Local Media:** a media index with a compact identity block, filter command
  cells, acquisition focus, and warm signal placeholders.
- **Local/Network Files:** a path rail above list or grid content, with folder
  and media objects treated as scanned records.
- **Network Sources:** a route catalogue paired with a connection telemetry
  panel and explicit security state.
- **Settings:** five numbered command banks and dense, stable rows. Subtitle
  controls and their Western, Cyrillic, Japanese, Chinese, and Korean preview
  remain visible without changing their input model.
- **Video:** the picture remains dominant; the HUD is a temporary acquisition
  layer with square transport cells and a full-height track/settings drawer.
- **Music:** cover-derived colour fields remain unique to each track, with a
  sparse wire signal layer and acquisition marks around the artwork.
- **Loading and errors:** full scenes, not detached modal cards, centered on an
  acquisition target or fault cell.

## Typography and assets

Inter Medium and SemiBold render Western and Cyrillic runs at their requested
pixel size. Native PS Vita PGFs render Japanese, Chinese, and Korean runs, with
a fully native system-font preference available for all subtitles. No artwork
from the visual reference is packaged in the application; particles, lines,
corners, panels, and focus effects are drawn by vita2d.

The application icon condenses the same grammar into a flat segmented shell:
amber aperture plates surround a hot-white playback core, while two restrained
cyan telemetry arcs identify machine state. Its 128-pixel indexed export keeps
the silhouette readable in LiveArea without importing imagery from the visual
reference.
