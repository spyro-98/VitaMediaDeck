#ifndef VITAMEDIADECK_UI_THEME_H
#define VITAMEDIADECK_UI_THEME_H

#include <vita2d.h>

/* "Midnight Halo": sampled from the 2026-08-09 VitaMediaDeck icon.  The old
 * red/yellow/green/blue brand corners are deliberately gone; decorative UI
 * uses only ink, steel-blue and the electric blue emitted by the play badge.
 * Green/amber/red remain reserved for semantic success/warning/error states. */
#define VT_THEME_BG             RGBA8(2, 7, 12, 255)
#define VT_THEME_BG_SOFT        RGBA8(5, 13, 22, 255)
#define VT_THEME_MEDIA_BACKDROP RGBA8(1, 5, 9, 255)
#define VT_THEME_SURFACE        RGBA8(11, 23, 35, 255)
#define VT_THEME_SURFACE_RAISED RGBA8(18, 37, 56, 255)
#define VT_THEME_SURFACE_FOCUS  RGBA8(15, 56, 91, 255)
#define VT_THEME_BORDER         RGBA8(41, 72, 96, 255)
#define VT_THEME_BORDER_DIM     RGBA8(25, 47, 65, 255)

#define VT_THEME_BLUE_DIM       RGBA8(10, 57, 98, 255)
#define VT_THEME_BLUE           RGBA8(23, 103, 164, 255)
#define VT_THEME_BLUE_BRIGHT    RGBA8(42, 143, 212, 255)
#define VT_THEME_BLUE_LIGHT     RGBA8(103, 184, 238, 255)
#define VT_THEME_BLUE_A(a)      RGBA8(35, 126, 194, (a))
#define VT_THEME_HALO_A(a)      RGBA8(37, 139, 221, (a))

#define VT_THEME_TEXT           RGBA8(248, 251, 254, 255)
/* Secondary copy stays comfortably readable on both base and raised surfaces
 * at the Vita's native 960x544 resolution.  FAINT is reserved for metadata;
 * actionable labels must use TEXT or TEXT_MUTED. */
#define VT_THEME_TEXT_MUTED     RGBA8(185, 204, 220, 255)
#define VT_THEME_TEXT_FAINT     RGBA8(132, 158, 179, 255)

/* Semantic colours are intentionally not used as decoration. */
#define VT_THEME_SUCCESS        RGBA8(69, 190, 126, 255)
#define VT_THEME_WARNING        RGBA8(236, 174, 76, 255)
#define VT_THEME_DANGER         RGBA8(235, 93, 108, 255)

#endif /* VITAMEDIADECK_UI_THEME_H */
