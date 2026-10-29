#ifndef HOST_PROMPT_VITA2D_H
#define HOST_PROMPT_VITA2D_H
#include <stdint.h>
#define RGBA8(r,g,b,a) ((unsigned)(r)|((unsigned)(g)<<8)|((unsigned)(b)<<16)|((unsigned)(a)<<24))
typedef void vita2d_font;
typedef void vita2d_pgf;
typedef struct HostTexture vita2d_texture;
typedef struct {int language; void *reserved;} vita2d_system_pgf_config;
int vita2d_font_text_width(vita2d_font*,unsigned,const char*);
int vita2d_font_text_height(vita2d_font*,unsigned,const char*);
int vita2d_font_draw_text(vita2d_font*,int,int,unsigned,unsigned,const char*);
int vita2d_pgf_text_width(vita2d_pgf*,float,const char*);
int vita2d_pgf_text_height(vita2d_pgf*,float,const char*);
int vita2d_pgf_draw_text(vita2d_pgf*,int,int,unsigned,float,const char*);
vita2d_pgf *vita2d_load_system_pgf(int,const vita2d_system_pgf_config*);
void vita2d_free_pgf(vita2d_pgf*);
vita2d_texture *vita2d_load_PNG_file(const char*);
void vita2d_free_texture(vita2d_texture*);
void vita2d_draw_fill_circle(float,float,float,unsigned);
void vita2d_draw_rectangle(float,float,float,float,unsigned);
void vita2d_draw_texture_tint_part_scale(const vita2d_texture*,float,float,float,float,float,float,float,float,unsigned);
#endif
