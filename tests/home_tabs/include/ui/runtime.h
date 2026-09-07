#include <vita2d.h>
#define UI_FONT_SMALL 16
#define UI_FONT_BODY 20
vita2d_font *ui_runtime_font(unsigned);
int ui_font_text_width(vita2d_font*,unsigned,const char*);
int ui_font_fit_text(vita2d_font*,unsigned,const char*,char*,size_t,int);
int ui_font_draw_text(vita2d_font*,int,int,unsigned,unsigned,const char*);
