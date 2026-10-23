#pragma once
#include "psp2/gxm.h"
typedef struct {SceGxmTexture gxm_tex;} vita2d_texture;
void vita2d_wait_rendering_done(void);
void vita2d_texture_set_filters(vita2d_texture*,int,int);
void vita2d_draw_texture_part_scale(const vita2d_texture*,float,float,float,float,float,float,float,float);

void vita2d_draw_texture_scale(const vita2d_texture*,float,float,float,float);
