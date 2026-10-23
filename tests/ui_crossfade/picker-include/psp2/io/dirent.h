#pragma once
#include <psp2/kernel/threadmgr.h>
#define SCE_S_ISDIR(mode) ((mode)==1)
typedef struct { struct { int st_mode; } d_stat; char d_name[256]; } SceIoDirent;
SceUID sceIoDopen(const char *);
int sceIoDclose(SceUID);
int sceIoDread(SceUID,SceIoDirent *);
int sceIoMkdir(const char *,int);
