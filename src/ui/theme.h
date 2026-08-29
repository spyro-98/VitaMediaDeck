#ifndef VITAMEDIADECK_UI_THEME_H
#define VITAMEDIADECK_UI_THEME_H

#include <vita2d.h>

/* "Signal / Shell": a Vita-native interpretation of the black-glass,
 * particulate amber and white acquisition graphics used in Stylow's Ghost in
 * the Shell concepts.  It deliberately avoids the generic cyan sci-fi HUD.
 * Existing BLUE names remain compatibility aliases so every older scene adopts
 * the new visual system without changing its input or feature contracts. */
#define VT_THEME_BG             RGBA8(5, 5, 6, 255)       /* Obsidian */
#define VT_THEME_BG_SOFT        RGBA8(10, 10, 11, 255)    /* Carbon */
#define VT_THEME_MEDIA_BACKDROP RGBA8(2, 2, 3, 255)
#define VT_THEME_SURFACE        RGBA8(17, 17, 18, 246)
#define VT_THEME_SURFACE_RAISED RGBA8(27, 25, 23, 250)
#define VT_THEME_SURFACE_FOCUS  RGBA8(53, 39, 23, 248)
#define VT_THEME_BORDER         RGBA8(91, 75, 55, 255)
#define VT_THEME_BORDER_DIM     RGBA8(48, 42, 34, 255)

#define VT_THEME_SIGNAL_DIM     RGBA8(117, 61, 29, 255)
#define VT_THEME_SIGNAL         RGBA8(201, 91, 38, 255)
#define VT_THEME_SIGNAL_BRIGHT  RGBA8(255, 178, 62, 255)
#define VT_THEME_SIGNAL_LIGHT   RGBA8(255, 218, 151, 255)
#define VT_THEME_SIGNAL_A(a)    RGBA8(255, 156, 46, (a))
#define VT_THEME_PARTICLE_A(a)  RGBA8(255, 190, 83, (a))

/* Cold telemetry is deliberately secondary: amber remains the human focus
 * signal, while cyan identifies machine state, scans, locks, and buffers. */
#define VT_THEME_COLD_DIM        RGBA8(28, 93, 115, 255)
#define VT_THEME_COLD            RGBA8(58, 186, 217, 255)
#define VT_THEME_COLD_LIGHT      RGBA8(184, 241, 255, 255)
#define VT_THEME_COLD_A(a)       RGBA8(82, 205, 232, (a))

#define VT_THEME_BLUE_DIM       VT_THEME_SIGNAL_DIM
#define VT_THEME_BLUE           VT_THEME_SIGNAL
#define VT_THEME_BLUE_BRIGHT    VT_THEME_SIGNAL_BRIGHT
#define VT_THEME_BLUE_LIGHT     VT_THEME_SIGNAL_LIGHT
#define VT_THEME_BLUE_A(a)      VT_THEME_SIGNAL_A(a)
#define VT_THEME_HALO_A(a)      VT_THEME_PARTICLE_A(a)

#define VT_THEME_TEXT           RGBA8(244, 241, 232, 255) /* Hot white */
/* Secondary copy is warm enough to sit inside the amber world without losing
 * the contrast needed for 16 px Vita text. */
#define VT_THEME_TEXT_MUTED     RGBA8(199, 193, 179, 255)
#define VT_THEME_TEXT_FAINT     RGBA8(139, 132, 119, 255)

/* Semantic colours remain distinct from the decorative signal amber. */
#define VT_THEME_SUCCESS        RGBA8(107, 211, 174, 255)
#define VT_THEME_WARNING        RGBA8(255, 190, 83, 255)
#define VT_THEME_DANGER         RGBA8(239, 96, 82, 255)

#endif /* VITAMEDIADECK_UI_THEME_H */
