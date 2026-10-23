#pragma once
#include <stdint.h>
typedef struct {uint32_t buttons;unsigned char lx,ly,rx,ry;} SceCtrlData;
int sceCtrlPeekBufferPositive(int,SceCtrlData*,int);
#define SCE_CTRL_UP 1
#define SCE_CTRL_DOWN 2
#define SCE_CTRL_LEFT 4
#define SCE_CTRL_RIGHT 8
#define SCE_CTRL_CROSS 16
#define SCE_CTRL_CIRCLE 32
#define SCE_CTRL_TRIANGLE 64
#define SCE_CTRL_SQUARE 128
#define SCE_CTRL_START 256
#define SCE_CTRL_LTRIGGER 512
#define SCE_CTRL_RTRIGGER 1024

#define SCE_CTRL_MODE_ANALOG 1
int sceCtrlSetSamplingMode(int mode);
