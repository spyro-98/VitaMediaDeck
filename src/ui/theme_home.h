#ifndef VMD_THEME_HOME_H
#define VMD_THEME_HOME_H
#include "ui/theme.h"
#include "ui/layout.h"
#include "ui/focus_glow.h"
typedef struct { int x,y,w,h; } UiThemeRect;
/* rows is the number of visible items; columns is shared by rendering/input. */
typedef struct { int x,y,w,step,rows,columns; } UiThemeHomeLayout;
void ui_theme_service_icon(float x,float y,float w,float h,int service);
void ui_theme_fx_background(unsigned base);
void ui_theme_fx_surface(float x,float y,float w,float h,unsigned fill,int selected);
void ui_theme_fx_focus(float x,float y,float w,float h);
UiThemeRect ui_theme_home_tab_rect(int theme,int position,int expanded);
UiThemeHomeLayout ui_theme_home_layout(int theme,int bottom);
UiThemeRect ui_theme_home_item_rect(int theme,int slot,int bottom);
int ui_theme_home_menu_columns(int theme);
int ui_theme_home_top(int theme,int selected,int top,int bottom);
void ui_theme_home_menu(int theme,int selected,const char *const labels[6],const unsigned char *categories,int focused,UiFocusMotion *motion);
void ui_theme_home_heading(const char *title,const char *path,int bottom);
void ui_theme_home_row(int slot,int ordinal,const char *name,const char *detail,int selected,int focused,int bottom);
void ui_theme_home_row_art(int slot,int ordinal,const char *name,const char *detail,int selected,int focused,int bottom,vita2d_texture *art,const char *placeholder);
void ui_theme_home_preview(vita2d_texture *texture,const char *label,int bottom,int service);
void ui_theme_home_footer(const char *path,int bottom);
void ui_theme_home_footer_with_focus(const char *path,int bottom,int sections_focused);
const char *ui_theme_home_controls_hint(int sections_focused);
void ui_theme_home_position(int selected,int count,int bottom);
void ui_theme_home_service(int slot,int bottom,int service);
typedef struct { unsigned tabs,content; int focused; } UiThemeHomeNav;
UiThemeHomeNav ui_theme_home_navigation(int theme,int focused,int count,unsigned nav,unsigned rail_nav);
#endif
