#ifndef VITAMEDIADECK_UI_COMPONENTS_H
#define VITAMEDIADECK_UI_COMPONENTS_H

#include <vita2d.h>

/* Returns opaque black or white using perceived luminance. */
unsigned int ui_contrast_bw(unsigned int background);

/* Structural signal field shared by library and source-management screens. */
void ui_chrome_background(unsigned int base, unsigned int accent);

/* Compact scene identity used where a screen has both a local title and
 * controls in the command band. Code is a short stable marker such as LIB/01;
 * title and detail remain translated by the caller. */
void ui_scene_identity(float x, float y, float width, const char *code,
	                   const char *title, const char *detail);

void ui_panel(float x, float y, float width, float height,
	          unsigned int fill, unsigned int accent, int focused);

/* Draws a physical-controller style action: a compact key cap plus its verb.
 * Foreground is always black or white, selected from the supplied fill. */
void ui_action_button(float x, float y, float width, float height,
	                  unsigned int fill, const char *key, const char *label,
	                  int active);

#endif /* VITAMEDIADECK_UI_COMPONENTS_H */
