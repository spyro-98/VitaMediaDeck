#ifndef VITAMEDIADECK_UI_FONT_H
#define VITAMEDIADECK_UI_FONT_H

#include <stddef.h>
#include <vita2d.h>

/* Mixed UTF-8 text rendering tuned for the Vita's native 960x544 output.
 * Latin, Greek and Cyrillic use exact-size Inter instances rendered by
 * FreeType with normal grid fitting. Japanese, Chinese and Korean use their
 * matching Vita system PGF, retaining the console's complete character sets.
 * Width calculations share the exact same routing as drawing, so clipping,
 * centering and marquee animation cannot drift from the rendered pixels. */
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

/* Fits one UTF-8 line into max_width and out_size, appending a real ellipsis
 * when any text was removed. The output is always NUL-terminated when
 * out_size is non-zero. Returns the rendered width of the fitted text. */
int ui_font_fit_text(vita2d_font *latin_font, unsigned int size,
	                 const char *text, char *out, size_t out_size,
	                 int max_width);

/* Convenience for the common centered, single-line label. Text is fitted
 * before centering, so long translations never start outside the viewport. */
int ui_font_draw_text_centered(vita2d_font *latin_font, int center_x, int y,
	                           int max_width, unsigned int color,
	                           unsigned int size, const char *text);

#endif /* VITAMEDIADECK_UI_FONT_H */
