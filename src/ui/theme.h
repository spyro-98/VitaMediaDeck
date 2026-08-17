#ifndef VITATUBE_UI_THEME_H
#define VITATUBE_UI_THEME_H

#include <vita2d.h>

/* "Midnight Halo": sampled from the 2026-08-09 VitaTube icon.  The old
 * red/yellow/green/blue brand corners are deliberately gone; decorative UI
 * uses only ink, steel-blue and the electric blue emitted by the play badge.
 * Green/amber/red remain reserved for semantic success/warning/error states. */
#define VT_THEME_BG             RGBA8(2, 6, 11, 255)
#define VT_THEME_BG_SOFT        RGBA8(4, 10, 18, 255)
#define VT_THEME_MEDIA_BACKDROP RGBA8(1, 4, 8, 255)
#define VT_THEME_SURFACE        RGBA8(10, 18, 29, 255)
#define VT_THEME_SURFACE_RAISED RGBA8(16, 27, 42, 255)
#define VT_THEME_SURFACE_FOCUS  RGBA8(12, 38, 67, 255)
#define VT_THEME_BORDER         RGBA8(30, 52, 75, 255)
#define VT_THEME_BORDER_DIM     RGBA8(20, 36, 53, 255)

#define VT_THEME_BLUE_DIM       RGBA8(8, 48, 88, 255)
#define VT_THEME_BLUE           RGBA8(18, 82, 139, 255)
#define VT_THEME_BLUE_BRIGHT    RGBA8(35, 119, 190, 255)
#define VT_THEME_BLUE_LIGHT     RGBA8(84, 158, 218, 255)
#define VT_THEME_BLUE_A(a)      RGBA8(26, 92, 153, (a))
#define VT_THEME_HALO_A(a)      RGBA8(22, 104, 183, (a))

#define VT_THEME_TEXT           RGBA8(247, 249, 252, 255)
#define VT_THEME_TEXT_MUTED     RGBA8(145, 164, 184, 255)
#define VT_THEME_TEXT_FAINT     RGBA8(103, 125, 148, 255)

/* Semantic colours are intentionally not used as decoration. */
#define VT_THEME_SUCCESS        RGBA8(69, 190, 126, 255)
#define VT_THEME_WARNING        RGBA8(236, 174, 76, 255)
#define VT_THEME_DANGER         RGBA8(235, 93, 108, 255)

#endif /* VITATUBE_UI_THEME_H */
