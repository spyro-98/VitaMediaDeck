#ifndef VMD_THUMB_VITA2D
#define VMD_THUMB_VITA2D
#include <stdint.h>
#include <stddef.h>
typedef struct {uint16_t pixels[264*148];} vita2d_texture;
void vita2d_wait_rendering_done(void);
void vita2d_free_texture(vita2d_texture *);
vita2d_texture *vita2d_create_empty_texture_format(unsigned,unsigned,int);
unsigned vita2d_texture_get_stride(const vita2d_texture *);
void *vita2d_texture_get_datap(const vita2d_texture *);
void vita2d_texture_set_filters(vita2d_texture *,int,int);
#endif
