#ifndef VMD_UI_BREADCRUMBS_H
#define VMD_UI_BREADCRUMBS_H

/* Stateless, allocation-free trail renderer shared by page and fading player
 * headers. Callers pass display labels, never credentials or transport URLs. */
void ui_breadcrumb_draw(int x, int right, const char *section,
                        const char *parent, const char *leaf, float opacity);

#endif
