#ifndef VITATUBE_UI_COMPONENTS_H
#define VITATUBE_UI_COMPONENTS_H

#include <vita2d.h>

/* Returns opaque black or white using perceived luminance. */
unsigned int ui_contrast_bw(unsigned int background);

/* Quiet layered background shared by library and source-management screens. */
void ui_chrome_background(unsigned int base, unsigned int accent);

void ui_panel(float x, float y, float width, float height,
	          unsigned int fill, unsigned int accent, int focused);

/* Draws a physical-controller style action: a compact key cap plus its verb.
 * Foreground is always black or white, selected from the supplied fill. */
void ui_action_button(float x, float y, float width, float height,
	                  unsigned int fill, const char *key, const char *label,
	                  int active);

#endif /* VITATUBE_UI_COMPONENTS_H */
