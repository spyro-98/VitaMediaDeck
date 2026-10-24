#ifndef VMD_HOME_TABS_H
#define VMD_HOME_TABS_H
#include "ui/touch.h"
#include "ui/focus_glow.h"
enum { HOME_LIBRARY, HOME_VIDEO, HOME_AUDIO, HOME_RADIO, HOME_PODCAST,
       HOME_IMAGES, HOME_TAB_COUNT };
#define HOME_PODCAST_ONLINE HOME_PODCAST
void ui_home_podcast_set_offline(int offline);
int ui_home_tab(void);
const char *ui_home_tab_label(void);
void ui_home_tab_set(int id);
int ui_home_tab_filter(void);
int ui_home_tab_online(void);
unsigned ui_home_tabs_navigation(unsigned pressed, unsigned held, unsigned char lx, unsigned char ly);
int ui_home_tabs_touch_hit(const UiTouchEvent *touch);
int ui_home_tabs_input(unsigned nav, unsigned flags, const UiTouchEvent *touch, int focused);
typedef struct {
 unsigned pressed,held,touch_flags;
 unsigned char lx,ly,rx,ry;
 int selected,count,bottom,focus_sections,blocked;
 int geometric_content; /* dashboard performs spatial edge navigation itself */
 const UiTouchEvent *touch;
} UiHomeControlSample;
typedef struct {unsigned pressed,content_nav;int focus_sections,sections_changed;} UiHomeControlResult;
void ui_home_controls_reset(void);
UiHomeControlResult ui_home_controls_update(const UiHomeControlSample *sample);
void ui_home_tabs_set_expanded(int expanded);
void ui_home_tabs_draw(int focused);
void ui_home_tabs_draw_with_focus(int focused, UiFocusMotion *motion);
int ui_home_tabs_configure(void);
#endif
