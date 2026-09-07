#ifndef VMD_HOME_TABS_H
#define VMD_HOME_TABS_H
#include "ui/touch.h"
enum { HOME_LIBRARY, HOME_VIDEO, HOME_AUDIO, HOME_RADIO, HOME_PODCAST_ONLINE,
       HOME_PODCAST_OFFLINE, HOME_IMAGES, HOME_TAB_COUNT };
int ui_home_tab(void);
void ui_home_tab_set(int id);
int ui_home_tab_filter(void);
int ui_home_tab_online(void);
unsigned ui_home_tabs_navigation(unsigned pressed, unsigned held, unsigned char lx, unsigned char ly);
int ui_home_tabs_input(unsigned nav, unsigned flags, const UiTouchEvent *touch, int focused);
void ui_home_tabs_draw(int focused);
int ui_home_tabs_configure(void);
#endif
