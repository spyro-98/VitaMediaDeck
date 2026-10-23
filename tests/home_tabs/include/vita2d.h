#ifndef VMD_TEST_VITA2D
#define VMD_TEST_VITA2D
#include <stddef.h>
#include <stdint.h>
typedef struct {float x,y,z;unsigned color;} vita2d_color_vertex;
typedef struct {float x,y,z,u,v;} vita2d_texture_vertex;
typedef unsigned SceGxmPrimitiveType;
#define SCE_GXM_PRIMITIVE_TRIANGLES 1
void vita2d_draw_array(SceGxmPrimitiveType,const vita2d_color_vertex*,size_t);
typedef void vita2d_font;
typedef void vita2d_texture;
unsigned vita2d_texture_get_width(const vita2d_texture*);
unsigned vita2d_texture_get_height(const vita2d_texture*);
void vita2d_draw_array_textured(const vita2d_texture*,SceGxmPrimitiveType,const vita2d_texture_vertex*,size_t,unsigned);
#define RGBA8(r,g,b,a) ((unsigned)(r)|((unsigned)(g)<<8)|((unsigned)(b)<<16)|((unsigned)(a)<<24))
void vita2d_draw_rectangle(float,float,float,float,unsigned);
void vita2d_draw_line(float,float,float,float,unsigned);
void vita2d_set_clip_rectangle(int,int,int,int);
void vita2d_enable_clipping(void);
void vita2d_disable_clipping(void);
void vita2d_start_drawing(void);
void vita2d_clear_screen(void);
void vita2d_end_drawing(void);
void vita2d_wait_rendering_done(void);
void vita2d_swap_buffers(void);
#endif
