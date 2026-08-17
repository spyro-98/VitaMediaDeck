#ifndef VITATUBE_UI_BRAND_H
#define VITATUBE_UI_BRAND_H

#include <stddef.h>

/* VitaTube header shared by search, results, and the IME keyboard. */
void ui_brand_draw_header(const char *query);
void ui_brand_draw_header_placeholder(const char *query, const char *placeholder);
void ui_brand_set_loading(int loading);
void ui_brand_draw_search_backdrop(const char *query);
/* Variant used while sceIme is open: doesn't show the placeholder and draws
 * the caret at the current UTF-8 position, scrolling the text horizontally
 * when it no longer fits in the field. */
void ui_brand_draw_search_backdrop_editing(const char *query,
                                           size_t caret_byte,
                                           int caret_visible);
void ui_brand_draw_search_backdrop_editing_label(const char *query,
                                                 size_t caret_byte,
                                                 int caret_visible,
                                                 const char *title,
                                                 const char *hint);

/* Hit areas shared with the results screen: keeping the coordinates here
 * prevents drawing and touch from going out of sync when the bar
 * changes size. */
int ui_brand_search_field_hit(int x, int y);
int ui_brand_search_clear_hit(int x, int y);
void ui_brand_draw_status_indicators(void);
/* Player variant: follows the HUD fade without changing the fully opaque
 * indicators used by normal application headers. Values are clamped to 0..1;
 * zero returns before sampling the cached system status. */
void ui_brand_draw_status_indicators_alpha(float opacity);

#define UI_BRAND_HEADER_HEIGHT 54

#endif /* VITATUBE_UI_BRAND_H */
