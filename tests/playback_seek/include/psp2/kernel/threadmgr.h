#pragma once
#include "psp2/types.h"
int sceKernelGetThreadCurrentPriority(void);
SceUID sceKernelGetThreadId(void);
int sceKernelChangeThreadPriority(SceUID,int);
int sceKernelCreateThread(const char*,int(*)(SceSize,void*),int,unsigned,int,int,void*);
int sceKernelStartThread(SceUID,SceSize,void*);
int sceKernelWaitThreadEnd(SceUID,void*,void*);
int sceKernelDeleteThread(SceUID);
int sceKernelExitThread(int);

int sceKernelDelayThread(unsigned);
