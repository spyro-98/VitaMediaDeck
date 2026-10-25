#ifndef HOST_VITA2D_H
#define HOST_VITA2D_H
#include <stdint.h>
#define RGBA8(r, g, b, a)                                                                          \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))
typedef struct vita2d_texture vita2d_texture;
unsigned int vita2d_texture_get_width(const vita2d_texture *);
unsigned int vita2d_texture_get_height(const vita2d_texture *);
int vita2d_common_dialog_update(void);
#endif
