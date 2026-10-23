#ifndef VITAMEDIADECK_UI_THEME_H
#define VITAMEDIADECK_UI_THEME_H
#include <vita2d.h>
#include "settings/preferences.h"
/* One immutable palette selected at startup or after a committed setting.
 * Reads are array loads: no per-frame preferences I/O or heap allocations. */
enum UiThemeColor {
 UI_COLOR_BG,
 UI_COLOR_BG_SOFT,
 UI_COLOR_MEDIA_BACKDROP,
 UI_COLOR_SURFACE,
 UI_COLOR_SURFACE_RAISED,
 UI_COLOR_SURFACE_FOCUS,
 UI_COLOR_BORDER,
 UI_COLOR_BORDER_DIM,
 UI_COLOR_FOCUS,
 UI_COLOR_SIGNAL_DIM,
 UI_COLOR_SIGNAL,
 UI_COLOR_SIGNAL_BRIGHT,
 UI_COLOR_SIGNAL_LIGHT,
 UI_COLOR_WARM_DIM,
 UI_COLOR_WARM,
 UI_COLOR_WARM_LIGHT,
 UI_COLOR_SPECTRAL,
 UI_COLOR_SPECTRAL_LIGHT,
 UI_COLOR_COLD_DIM,
 UI_COLOR_COLD,
 UI_COLOR_COLD_LIGHT,
 UI_COLOR_BLUE_DIM,
 UI_COLOR_BLUE,
 UI_COLOR_BLUE_BRIGHT,
 UI_COLOR_TEXT,
 UI_COLOR_TEXT_MUTED,
 UI_COLOR_TEXT_FAINT,
 UI_COLOR_SUCCESS,
 UI_COLOR_WARNING,
 UI_COLOR_DANGER,
 UI_COLOR_GLASS_A,
 UI_COLOR_FOCUS_A,
 UI_COLOR_SIGNAL_A,
 UI_COLOR_WARM_A,
 UI_COLOR_SPECTRAL_A,
 UI_COLOR_COLD_A,
 UI_COLOR_BLUE_A,
 UI_COLOR_COUNT
};
extern const unsigned int *ui_theme_colors;
void ui_theme_apply(int theme);
int ui_theme_id(void);
const char *ui_theme_name(int theme);
/* Video/image HUDs keep readable dark surfaces even with the light theme. */
const unsigned int *ui_theme_media_begin(void);
void ui_theme_media_end(const unsigned int *previous);
static inline unsigned ui_theme_alpha(unsigned color,unsigned alpha) {
 return (color & 0x00ffffffU) | ((alpha>255?255:alpha)<<24);
}
#define VT_THEME_BG ui_theme_colors[UI_COLOR_BG]
#define VT_THEME_BG_SOFT ui_theme_colors[UI_COLOR_BG_SOFT]
#define VT_THEME_MEDIA_BACKDROP ui_theme_colors[UI_COLOR_MEDIA_BACKDROP]
#define VT_THEME_SURFACE ui_theme_colors[UI_COLOR_SURFACE]
#define VT_THEME_SURFACE_RAISED ui_theme_colors[UI_COLOR_SURFACE_RAISED]
#define VT_THEME_SURFACE_FOCUS ui_theme_colors[UI_COLOR_SURFACE_FOCUS]
#define VT_THEME_BORDER ui_theme_colors[UI_COLOR_BORDER]
#define VT_THEME_BORDER_DIM ui_theme_colors[UI_COLOR_BORDER_DIM]
#define VT_THEME_FOCUS ui_theme_colors[UI_COLOR_FOCUS]
#define VT_THEME_SIGNAL_DIM ui_theme_colors[UI_COLOR_SIGNAL_DIM]
#define VT_THEME_SIGNAL ui_theme_colors[UI_COLOR_SIGNAL]
#define VT_THEME_SIGNAL_BRIGHT ui_theme_colors[UI_COLOR_SIGNAL_BRIGHT]
#define VT_THEME_SIGNAL_LIGHT ui_theme_colors[UI_COLOR_SIGNAL_LIGHT]
#define VT_THEME_WARM_DIM ui_theme_colors[UI_COLOR_WARM_DIM]
#define VT_THEME_WARM ui_theme_colors[UI_COLOR_WARM]
#define VT_THEME_WARM_LIGHT ui_theme_colors[UI_COLOR_WARM_LIGHT]
#define VT_THEME_SPECTRAL ui_theme_colors[UI_COLOR_SPECTRAL]
#define VT_THEME_SPECTRAL_LIGHT ui_theme_colors[UI_COLOR_SPECTRAL_LIGHT]
#define VT_THEME_COLD_DIM ui_theme_colors[UI_COLOR_COLD_DIM]
#define VT_THEME_COLD ui_theme_colors[UI_COLOR_COLD]
#define VT_THEME_COLD_LIGHT ui_theme_colors[UI_COLOR_COLD_LIGHT]
#define VT_THEME_BLUE_DIM ui_theme_colors[UI_COLOR_BLUE_DIM]
#define VT_THEME_BLUE ui_theme_colors[UI_COLOR_BLUE]
#define VT_THEME_BLUE_BRIGHT ui_theme_colors[UI_COLOR_BLUE_BRIGHT]
#define VT_THEME_TEXT ui_theme_colors[UI_COLOR_TEXT]
#define VT_THEME_TEXT_MUTED ui_theme_colors[UI_COLOR_TEXT_MUTED]
#define VT_THEME_TEXT_FAINT ui_theme_colors[UI_COLOR_TEXT_FAINT]
#define VT_THEME_SUCCESS ui_theme_colors[UI_COLOR_SUCCESS]
#define VT_THEME_WARNING ui_theme_colors[UI_COLOR_WARNING]
#define VT_THEME_DANGER ui_theme_colors[UI_COLOR_DANGER]
#define VT_THEME_GLASS_A(a) ui_theme_alpha(ui_theme_colors[UI_COLOR_GLASS_A],(a))
#define VT_THEME_FOCUS_A(a) ui_theme_alpha(ui_theme_colors[UI_COLOR_FOCUS_A],(a))
#define VT_THEME_SIGNAL_A(a) ui_theme_alpha(ui_theme_colors[UI_COLOR_SIGNAL_A],(a))
#define VT_THEME_WARM_A(a) ui_theme_alpha(ui_theme_colors[UI_COLOR_WARM_A],(a))
#define VT_THEME_SPECTRAL_A(a) ui_theme_alpha(ui_theme_colors[UI_COLOR_SPECTRAL_A],(a))
#define VT_THEME_COLD_A(a) ui_theme_alpha(ui_theme_colors[UI_COLOR_COLD_A],(a))
#define VT_THEME_BLUE_A(a) ui_theme_alpha(ui_theme_colors[UI_COLOR_BLUE_A],(a))
#define VT_THEME_PARTICLE_A(a) VT_THEME_SPECTRAL_A(a)
#define VT_THEME_BLUE_LIGHT VT_THEME_SIGNAL_BRIGHT
#define VT_THEME_HALO_A(a) VT_THEME_SPECTRAL_A(a)

#endif
