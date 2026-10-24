#ifndef VMD_HOME_DASHBOARD_H
#define VMD_HOME_DASHBOARD_H
#include "ui/theme_home.h"
enum {
    UI_DASH_CONTINUE,
    UI_DASH_CONNECTIONS,
    UI_DASH_FAVOURITES,
    UI_DASH_RECENT,
    UI_DASH_SOURCES,
    UI_DASH_HISTORY,
    UI_DASH_COUNT
};
typedef struct {
    UiThemeRect content, cards[UI_DASH_COUNT];
    int title_y, subtitle_y, footer_y;
} UiDashboardLayout;
UiDashboardLayout ui_dashboard_layout(int theme, int bottom);
int ui_dashboard_move(const UiDashboardLayout *, int selected, unsigned direction);
#endif
