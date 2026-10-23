#ifndef VMD_SEEK_VITA2D
#define VMD_SEEK_VITA2D
#include "../../../online_navigation/include/vita2d.h"
#include <psp2/gxm.h>
vita2d_texture *vita2d_create_empty_texture_format(unsigned,unsigned,int);
unsigned vita2d_texture_get_stride(const vita2d_texture *);
void *vita2d_texture_get_datap(const vita2d_texture *);
void vita2d_texture_set_filters(vita2d_texture *,int,int);
void vita2d_free_texture(vita2d_texture *);
void vita2d_draw_fill_circle(float,float,float,unsigned);
#endif
