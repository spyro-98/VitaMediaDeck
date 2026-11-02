#pragma once
#include "psp2/types.h"
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE 1
SceUID sceKernelAllocMemBlock(const char *,int,unsigned,void *);
int sceKernelGetMemBlockBase(SceUID,void **);
int sceKernelFreeMemBlock(SceUID);
