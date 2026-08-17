#ifndef VITATUBE_UI_FONT_H
#define VITATUBE_UI_FONT_H

#include <vita2d.h>

/* Mixed UTF-8 text rendering. Latin runs stay on the caller-provided
 * Poppins TTF; CJK runs use the Vita system PGF when available. All width
 * calculations use the same run splitter as drawing, so clipping, centering
 * and marquee animation cannot drift from the rendered result. */
int ui_font_fallback_init(void);
void ui_font_fallback_term(void);
int ui_font_fallback_ready(void);
int ui_font_fallback_language_mask(void);

int ui_font_draw_text(vita2d_font *latin_font, int x, int y,
	                  unsigned int color, unsigned int size,
	                  const char *text);

__attribute__((__format__(__printf__, 6, 7)))
int ui_font_draw_textf(vita2d_font *latin_font, int x, int y,
	                   unsigned int color, unsigned int size,
	                   const char *format, ...);

void ui_font_text_dimensions(vita2d_font *latin_font, unsigned int size,
	                         const char *text, int *width, int *height);
int ui_font_text_width(vita2d_font *latin_font, unsigned int size,
	                   const char *text);
int ui_font_text_height(vita2d_font *latin_font, unsigned int size,
	                    const char *text);

#endif /* VITATUBE_UI_FONT_H */
