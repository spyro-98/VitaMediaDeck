#ifndef VMD_CROSSFADE_VITA2D_H
#define VMD_CROSSFADE_VITA2D_H
#include "../../online_navigation/include/vita2d.h"
typedef unsigned SceGxmTextureFormat;
#define SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8 0x98000000U
#define SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8 0x0c000000U
#define SCE_GXM_TEXTURE_BASE_FORMAT_U8 0x00000000U
unsigned vita2d_texture_get_stride(const vita2d_texture *);
SceGxmTextureFormat vita2d_texture_get_format(const vita2d_texture *);
void *vita2d_texture_get_datap(const vita2d_texture *);
void vita2d_free_texture(vita2d_texture *);
void vita2d_draw_fill_circle(float,float,float,unsigned);
#endif
