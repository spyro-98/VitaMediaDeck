#ifndef VITAMEDIADECK_UI_RUNTIME_H
#define VITAMEDIADECK_UI_RUNTIME_H

#include <vita2d.h>

#include "ui/font.h"

/* Sole owner of the application's graphics subsystem.
 *
 * Vita2D/GXM is initialized once at startup and terminated once on exit.
 * AppUtil/CommonDialog is never explicitly initialized: on this app that
 * combination has already caused a hardware regression at startup.
 * Individual screens may create and free their own textures, but must
 * never call vita2d_init() or vita2d_fini(). */
int ui_runtime_init(void);
void ui_runtime_load_assets(void);
void ui_runtime_term(void);
int ui_runtime_is_ready(void);

/* Native-resolution type scale. Latin, Greek and Cyrillic use Inter; CJK uses
 * the matching Vita system PGF. Three TTF instances are intentional because
 * libvita2d caches each glyph at its first requested size and would otherwise
 * blur later sizes by scaling an existing atlas bitmap. */
#define UI_FONT_SMALL   16
#define UI_FONT_BODY    20
#define UI_FONT_DISPLAY 28

/* Resources shared for the whole session, owned by ui_runtime. */
vita2d_font *ui_runtime_font(unsigned int size);
vita2d_texture *ui_runtime_logo(void);

#endif /* VITAMEDIADECK_UI_RUNTIME_H */
