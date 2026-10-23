#pragma once
#include "psp2/types.h"
#define SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT 1
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 1
typedef struct {unsigned size,attr,alignment;} SceKernelAllocMemBlockOpt;
SceUID sceKernelAllocMemBlock(const char*,int,unsigned,SceKernelAllocMemBlockOpt*);
int sceKernelGetMemBlockBase(SceUID,void**);
int sceKernelFreeMemBlock(SceUID);
