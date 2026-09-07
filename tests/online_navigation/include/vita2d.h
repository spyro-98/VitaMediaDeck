#ifndef VMD_NAVIGATION_TEST_VITA2D_H
#define VMD_NAVIGATION_TEST_VITA2D_H
#include "../../home_tabs/include/vita2d.h"
typedef void vita2d_texture;
unsigned int vita2d_texture_get_width(const vita2d_texture *);
unsigned int vita2d_texture_get_height(const vita2d_texture *);
void vita2d_draw_texture_scale(const vita2d_texture *, float, float, float, float);
#endif
