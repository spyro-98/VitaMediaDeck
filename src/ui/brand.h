#ifndef VITAMEDIADECK_UI_BRAND_H
#define VITAMEDIADECK_UI_BRAND_H

#include <stddef.h>

/* VitaMediaDeck header shared by search, results, and the IME keyboard. */
void ui_brand_draw_header(const char *query);
/* Logo and system status, with the central area reserved for Home tabs. */
void ui_brand_draw_home_header(void);
void ui_brand_draw_home_search(void);
int ui_brand_home_search_hit(unsigned flags,int x,int y);
void ui_brand_draw_breadcrumbs(const char *section, const char *parent, const char *leaf);
void ui_brand_draw_path(const char *section, const char *path);
/* Uses the player's existing fading scrim, without an opaque logo/bar. */
void ui_brand_draw_player_breadcrumbs(const char *section, const char *parent,
                                      const char *leaf, float opacity);
int ui_brand_sections_hit(unsigned int touch_flags, int x, int y);
void ui_brand_draw_header_placeholder(const char *query, const char *placeholder);
void ui_brand_set_loading(int loading);
/* The active-download page already owns the central progress treatment. Hide
 * only the compact download ring for that frame; generic app loading remains
 * an independent signal. */
void ui_brand_set_download_progress_hidden(int hidden);
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

#endif /* VITAMEDIADECK_UI_BRAND_H */
