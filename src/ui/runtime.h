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
/* Loads the logo and one immediately usable font before the first frame. The
 * remaining font tiers can then initialize behind a complete startup scene
 * instead of leaving blank top-bar and text placeholders. */
void ui_runtime_load_boot_assets(void);
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
#define UI_FONT_SUBTITLE_LARGE 32
#define UI_FONT_SUBTITLE_EXTRA_LARGE 40

/* Resources shared for the whole session, owned by ui_runtime. */
vita2d_font *ui_runtime_font(unsigned int size);
/* Subtitle faces are loaded as independent exact-size instances so switching
 * weight never reuses a blurry atlas created at another size. Variant values
 * match VT_SUBTITLE_FONT_*; system variants return NULL intentionally and are
 * rendered by ui/font.c through native Vita PGFs. */
vita2d_font *ui_runtime_subtitle_font(int variant, unsigned int size);
vita2d_texture *ui_runtime_logo(void);

#endif /* VITAMEDIADECK_UI_RUNTIME_H */
