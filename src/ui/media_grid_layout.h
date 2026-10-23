#ifndef VMD_MEDIA_GRID_LAYOUT_H
#define VMD_MEDIA_GRID_LAYOUT_H
#include "ui/layout.h"

enum { UI_MEDIA_GRID_GAP = 8, UI_MEDIA_GRID_FOCUS_OUTSET = 3 };
typedef struct {
 int rows, card_h, thumb_h, title_y, detail_y, step;
} UiMediaGridLayout;

/* Two equal rows fill the actual browser viewport, including the mini-player.
 * The outer focus frame owns three pixels beyond the card; reserve those at
 * the bottom as well as the existing header/content gutter at the top. */
static inline UiMediaGridLayout ui_media_grid_layout(int top,int bottom) {
 int available=bottom-top-UI_MEDIA_GRID_FOCUS_OUTSET-1;
 if(available<1)available=1;
 int rows=available>=2*120+UI_MEDIA_GRID_GAP?2:1;
 int height=(available-(rows-1)*UI_MEDIA_GRID_GAP)/rows;
 int thumb=height-68;
 if(thumb<1)thumb=1;
 return (UiMediaGridLayout){rows,height,thumb,height-42,height-16,
                           height+UI_MEDIA_GRID_GAP};
}
#endif
