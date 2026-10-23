#pragma once
#include "psp2/types.h"
#define SCE_EVENT_WAITOR 1
#define SCE_EVENT_WAITCLEAR_PAT 4
int sceKernelGetThreadCurrentPriority(void);
SceUID sceKernelGetThreadId(void);
int sceKernelChangeThreadPriority(SceUID,int);
SceUID sceKernelCreateEventFlag(const char*,int,int,void*);
int sceKernelSetEventFlag(SceUID,unsigned);
int sceKernelClearEventFlag(SceUID,unsigned);
int sceKernelWaitEventFlag(SceUID,unsigned,unsigned,unsigned*,SceUInt*);
int sceKernelDeleteEventFlag(SceUID);
