#ifndef VMD_ONLINE_CATALOG_LAYOUT_H
#define VMD_ONLINE_CATALOG_LAYOUT_H
#include "network/online_audio.h"
#include "ui/layout.h"
enum { VT_CATALOG_STEP = 80, VT_CATALOG_HEIGHT = 72 };
static inline int vt_catalog_columns(int kind) {
    return kind == VT_ONLINE_COUNTRY ? 3 : kind == VT_ONLINE_EPISODE ? 1 : 2;
}
static inline int vt_catalog_rows(int y, int bottom) {
    int rows = (bottom - 28 - y + UI_CONTENT_GAP) / VT_CATALOG_STEP;
    return rows > 0 ? rows : 1;
}
static inline int vt_catalog_step(int y, int bottom) {
    return (bottom - 28 - y + UI_CONTENT_GAP) / vt_catalog_rows(y, bottom);
}
static inline int vt_catalog_width(int columns) { return (UI_CONTENT_W - (columns - 1) * UI_CONTENT_GAP) / columns; }
static inline int vt_catalog_top(int selected, int top, int columns, int rows) {
    if (top < 0) top = 0;
    top -= top % columns;
    if (selected < top) top = selected / columns * columns;
    if (selected >= top + rows * columns) top = (selected / columns - rows + 1) * columns;
    return top;
}
#endif
