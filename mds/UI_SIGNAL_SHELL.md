# Spectral Reassembly UI direction

VitaMediaDeck's original **Signal / Shell** system has evolved into **Spectral
Reassembly**. It is a code-native interpretation of the color, particle
material, optical depth, and holographic restraint in the three selected
*Ghost in the Shell* concept references and the
[Pushing Pixels interview with Stylow](https://www.pushing-pixels.org/2019/04/19/the-art-and-craft-of-screen-graphics-interview-with-stylow.html).
It does not reproduce film frames, characters, logos, or interface assets.

## Visual grammar

- **OLED void `#000000`:** the page background is true pitch black, allowing
  unused Vita OLED pixels to switch off. Dark teal exists only inside compact
  glass surfaces and never becomes a full-screen wash.
- **Abyss glass `#03080A` and `#061113`:** headers, drawers, mini-player chrome,
  and raised controls form a restrained optical layer above the black field.
- **Spectral white `#DEECEC`:** text, acquisition glints, and the brightest
  particles carry the silver material seen in the references.
- **Hologram amber `#C6651F` / `#EB9439`:** human focus and projected data use
  amber in thin rails, point clouds, and small control feedback—not large
  orange plates.
- **Reflected teal `#1D4D54` / `#379199`:** machine state, scans, buffering,
  locks, folders, and network transport use cold environmental reflection.
- Green, yellow, and red remain semantic status colors rather than decoration.

Particles appear in three material families: spectral dust defines an object's
surface, amber grains disclose active data, and teal packets identify machine
movement. Incomplete optical rings, fractured lattice points, thin scan seams,
and sparse wire connections replace continuous neon glows.

## Layout and motion

The existing scene hierarchy remains functional: command header, content field,
full-height L1 section index, and contextual R1 drawer. The memorable element is
the **reassembly field**. Focused records acquire a bounded cloud of fragments
that converges around their edges; loading and music scenes reuse the same
material without turning the screen into decorative concept art.

All fields are deterministic, allocation-free, and bounded for the 960x544 Vita
target. Reduce motion freezes particle drift, optical-ring movement, and scan
phases while retaining every static focus and state indicator.

## Scene interpretation

- **Local Media and Files:** covers remain dominant; empty media records use
  cold glass and spectral playback marks. Focus materializes around the record
  instead of filling it with orange.
- **Network Sources:** route controls and trust state use teal machine light,
  with amber reserved for the currently projected action.
- **Settings:** stable rows and multilingual previews sit on compact abyss-glass
  surfaces; the pitch-black field remains visible between control groups.
- **Video:** the picture remains dominant. Temporary HUD scrims stay black,
  while timelines, buffering, lock state, and the R1 drawer use the shared
  spectral/amber/teal hierarchy.
- **Music:** album colors illuminate only fine particles and two-pixel optical
  rings. The former full-screen color wash has been removed to preserve OLED
  black and reliable white text contrast.
- **Mini-player:** a spectral top seam, short amber projection rail, and small
  multicolor packets make the dock feel assembled into the current scene.
- **Loading and errors:** acquisition corners mix spectral and amber material;
  the particle field communicates activity without a generic neon spinner.

## Typography and assets

Inter Medium and SemiBold render Western and Cyrillic runs at exact requested
pixel sizes. Native PS Vita PGFs render Japanese, Chinese, and Korean, with a
fully native system-font preference available for subtitles. No reference
image is packaged in the application: particles, rings, lines, panels, and
focus fields are drawn by vita2d.

The active icon condenses the same grammar into an incomplete optical media
core: a dark central sphere, spectral-white glass, amber fractured data, and
restrained teal refraction on pitch black. Its 128x128 indexed export preserves
the particle silhouette in LiveArea without importing imagery or branding from
the references.
