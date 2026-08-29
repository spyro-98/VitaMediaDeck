#ifndef VITAMEDIADECK_UI_THEME_H
#define VITAMEDIADECK_UI_THEME_H

#include <vita2d.h>

/* "Spectral Reassembly": a Vita-native interpretation of holographic glass,
 * spectral particles, amber point-cloud projections and cold reflections.  The
 * full-screen base is deliberately pitch black so unused OLED pixels turn off;
 * colour exists only in surfaces, data and moving acquisition fields. Existing
 * BLUE names remain compatibility aliases so every scene adopts the system
 * without changing its input or feature contracts. */
#define VT_THEME_BG             RGBA8(0, 0, 0, 255)       /* OLED black */
#define VT_THEME_BG_SOFT        RGBA8(3, 8, 10, 255)      /* Abyss glass */
#define VT_THEME_MEDIA_BACKDROP RGBA8(0, 0, 0, 255)
#define VT_THEME_SURFACE        RGBA8(4, 12, 18, 246)
#define VT_THEME_SURFACE_RAISED RGBA8(7, 22, 31, 250)
#define VT_THEME_SURFACE_FOCUS  RGBA8(15, 34, 43, 248)
#define VT_THEME_BORDER         RGBA8(50, 79, 91, 255)
#define VT_THEME_BORDER_DIM     RGBA8(18, 39, 48, 255)
#define VT_THEME_GLASS_A(a)     RGBA8(7, 26, 35, (a))

/* Amber is projected data and human focus, not a large decorative fill. */
#define VT_THEME_SIGNAL_DIM     RGBA8(91, 55, 31, 255)
#define VT_THEME_SIGNAL         RGBA8(176, 94, 38, 255)
#define VT_THEME_SIGNAL_BRIGHT  RGBA8(224, 137, 60, 255)
#define VT_THEME_SIGNAL_LIGHT   RGBA8(255, 201, 124, 255)
#define VT_THEME_SIGNAL_A(a)    RGBA8(210, 113, 43, (a))

/* White/silver particles carry material highlights independently of amber. */
#define VT_THEME_SPECTRAL       RGBA8(222, 237, 236, 255)
#define VT_THEME_SPECTRAL_LIGHT RGBA8(248, 252, 249, 255)
#define VT_THEME_SPECTRAL_A(a)  RGBA8(231, 239, 232, (a))
#define VT_THEME_PARTICLE_A(a)  VT_THEME_SPECTRAL_A(a)

/* Teal is reflected environmental light and machine state. */
#define VT_THEME_COLD_DIM       RGBA8(19, 70, 91, 255)
#define VT_THEME_COLD           RGBA8(40, 137, 163, 255)
#define VT_THEME_COLD_LIGHT     RGBA8(139, 216, 228, 255)
#define VT_THEME_COLD_A(a)      RGBA8(54, 166, 190, (a))

#define VT_THEME_BLUE_DIM       VT_THEME_COLD_DIM
#define VT_THEME_BLUE           VT_THEME_COLD
#define VT_THEME_BLUE_BRIGHT    VT_THEME_COLD
#define VT_THEME_BLUE_LIGHT     VT_THEME_COLD_LIGHT
#define VT_THEME_BLUE_A(a)      VT_THEME_COLD_A(a)
#define VT_THEME_HALO_A(a)      VT_THEME_SPECTRAL_A(a)

#define VT_THEME_TEXT           RGBA8(226, 235, 232, 255)
#define VT_THEME_TEXT_MUTED     RGBA8(160, 176, 173, 255)
#define VT_THEME_TEXT_FAINT     RGBA8(94, 112, 112, 255)

/* Semantic colours remain distinct from the decorative signal amber. */
#define VT_THEME_SUCCESS        RGBA8(107, 211, 174, 255)
#define VT_THEME_WARNING        RGBA8(235, 166, 75, 255)
#define VT_THEME_DANGER         RGBA8(239, 96, 82, 255)

#endif /* VITAMEDIADECK_UI_THEME_H */
