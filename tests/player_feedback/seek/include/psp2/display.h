#ifndef VMD_SEEK_DISPLAY
#define VMD_SEEK_DISPLAY
#include <stddef.h>
typedef struct {unsigned size;void *base;unsigned pitch,pixelformat,width,height;} SceDisplayFrameBuf;
#define SCE_DISPLAY_SETBUF_IMMEDIATE 0
#define SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 0
int sceDisplayGetFrameBuf(SceDisplayFrameBuf *,int);
#endif
