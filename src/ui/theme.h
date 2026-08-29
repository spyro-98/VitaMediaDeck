#ifndef VITAMEDIADECK_UI_THEME_H
#define VITAMEDIADECK_UI_THEME_H

#include <vita2d.h>

/* "Spectral Reassembly": a Vita-native interpretation of holographic glass,
 * spectral particles, cyan point-cloud projections and cold reflections.  The
 * full-screen base is deliberately pitch black so unused OLED pixels turn off;
 * colour exists only in surfaces, data and moving acquisition fields. Existing
 * BLUE names remain compatibility aliases so every scene adopts the system
 * without changing its input or feature contracts. */
#define VT_THEME_BG             RGBA8(0, 0, 0, 255)       /* OLED black */
#define VT_THEME_BG_SOFT        RGBA8(2, 7, 12, 255)      /* Abyss glass */
#define VT_THEME_MEDIA_BACKDROP RGBA8(0, 0, 0, 255)
#define VT_THEME_SURFACE        RGBA8(3, 12, 20, 246)
#define VT_THEME_SURFACE_RAISED RGBA8(5, 21, 32, 250)
#define VT_THEME_SURFACE_FOCUS  RGBA8(8, 36, 49, 248)
#define VT_THEME_BORDER         RGBA8(38, 83, 100, 255)
#define VT_THEME_BORDER_DIM     RGBA8(13, 40, 53, 255)
#define VT_THEME_GLASS_A(a)     RGBA8(4, 27, 39, (a))

/* Spectral cyan is the primary interaction and projected-data signal. */
#define VT_THEME_SIGNAL_DIM     RGBA8(8, 63, 79, 255)
#define VT_THEME_SIGNAL         RGBA8(18, 151, 174, 255)
#define VT_THEME_SIGNAL_BRIGHT  RGBA8(43, 216, 233, 255)
#define VT_THEME_SIGNAL_LIGHT   RGBA8(184, 247, 251, 255)
#define VT_THEME_SIGNAL_A(a)    RGBA8(32, 190, 211, (a))

/* Oxidized amber is deliberately rare: warning semantics and isolated sparks. */
#define VT_THEME_WARM_DIM       RGBA8(70, 47, 29, 255)
#define VT_THEME_WARM           RGBA8(151, 91, 43, 255)
#define VT_THEME_WARM_LIGHT     RGBA8(213, 148, 78, 255)
#define VT_THEME_WARM_A(a)      RGBA8(175, 104, 47, (a))

/* White/silver particles carry material highlights independently of amber. */
#define VT_THEME_SPECTRAL       RGBA8(219, 239, 243, 255)
#define VT_THEME_SPECTRAL_LIGHT RGBA8(246, 253, 254, 255)
#define VT_THEME_SPECTRAL_A(a)  RGBA8(226, 244, 246, (a))
#define VT_THEME_PARTICLE_A(a)  VT_THEME_SPECTRAL_A(a)

/* Teal is reflected environmental light and machine state. */
#define VT_THEME_COLD_DIM       RGBA8(8, 48, 69, 255)
#define VT_THEME_COLD           RGBA8(18, 111, 145, 255)
#define VT_THEME_COLD_LIGHT     RGBA8(104, 203, 226, 255)
#define VT_THEME_COLD_A(a)      RGBA8(27, 137, 170, (a))

#define VT_THEME_BLUE_DIM       VT_THEME_COLD_DIM
#define VT_THEME_BLUE           VT_THEME_COLD
#define VT_THEME_BLUE_BRIGHT    VT_THEME_COLD
#define VT_THEME_BLUE_LIGHT     VT_THEME_COLD_LIGHT
#define VT_THEME_BLUE_A(a)      VT_THEME_COLD_A(a)
#define VT_THEME_HALO_A(a)      VT_THEME_SPECTRAL_A(a)

#define VT_THEME_TEXT           RGBA8(226, 235, 232, 255)
#define VT_THEME_TEXT_MUTED     RGBA8(160, 176, 173, 255)
#define VT_THEME_TEXT_FAINT     RGBA8(94, 112, 112, 255)

/* Semantic colours remain distinct from the decorative palette. */
#define VT_THEME_SUCCESS        RGBA8(107, 211, 174, 255)
#define VT_THEME_WARNING        RGBA8(235, 166, 75, 255)
#define VT_THEME_DANGER         RGBA8(239, 96, 82, 255)

#endif /* VITAMEDIADECK_UI_THEME_H */
