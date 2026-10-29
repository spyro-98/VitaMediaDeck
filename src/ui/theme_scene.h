#ifndef VMD_THEME_SCENE_H
#define VMD_THEME_SCENE_H
#include "ui/audio_layout.h"
#include "ui/page_motion.h"
#include "ui/theme_home.h"
/* Presentation channels have independent clocks and navigation identities. */
typedef enum {
    UI_SCENE_HOME,
    UI_SCENE_BROWSER,
    UI_SCENE_AUDIO,
    UI_SCENE_VIDEO,
    UI_SCENE_IMAGE,
    UI_SCENE_MENU,
    UI_SCENE_PICKER,
    UI_SCENE_SOURCES,
    UI_SCENE_QR,
    UI_SCENE_COUNT
} UiThemeScene;
/* Viewport coordinates are shared by drawing, focus and touch hit testing. */
UiPageView ui_theme_page(UiThemeScene scene, const char *owner, const char *identity,
    int first, int count, int columns, int slots, float step);
int ui_theme_page_input(UiThemeScene scene,const char *owner,int x,int y,int width,int height,
 int count,int first_rows,int *first,int *selected,unsigned buttons,unsigned char lx,unsigned char ly,
 unsigned *flags,const UiTouchEvent *touch);
void ui_theme_page_rebase(UiThemeScene scene,int before,int after);
UiThemeRect ui_theme_page_rect(UiThemeScene scene, UiThemeRect rect);
void ui_theme_page_finish(UiThemeScene scene, int x, int top, int width, int bottom);
void ui_theme_scene_background(void);
void ui_theme_scene_enter(UiThemeScene scene, const char *identity);
void ui_theme_scene_begin(UiThemeScene scene, const char *identity);
UiThemeRect ui_theme_scene_reveal(UiThemeScene scene, UiThemeRect rect, int slot);
void ui_theme_scene_model(UiThemeScene scene, float x, float y, float size, float opacity,
                          int running);
void ui_theme_scene_card(UiThemeScene role, UiThemeRect rect, int selected, int slot);
UiThemeHomeLayout ui_theme_browser_layout(int theme, int bottom, int grid);
UiThemeRect ui_theme_browser_rect(int theme, int slot, int bottom, int grid);
UiAudioLayout ui_theme_audio_layout(int layout, int has_cover);
typedef struct {
    float back_x, play_x, next_x, y;
    int seek_x, seek_y, seek_w;
} UiThemeVideoLayout;
UiThemeVideoLayout ui_theme_video_layout(void);
void ui_theme_scene_art(vita2d_texture *art, float x, float y, float size, int running);
void ui_theme_browser_tile(UiThemeScene scene, UiThemeRect r, int slot, int selected,
                           vita2d_texture *art, const char *title, const char *detail, int folder);
void ui_theme_video_scrim(float opacity);
void ui_theme_scene_transport(float x, float y, float w, float h, int selected, float opacity);
#endif
