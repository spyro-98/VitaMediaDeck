#ifndef VMD_TEST_VITA2D
#define VMD_TEST_VITA2D
#include <stddef.h>
#include <stdint.h>
typedef void vita2d_font;
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
