#include <psp2/videodec.h>
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 1
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW 2
#define SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT 1
typedef struct{uint32_t size,attr,alignment;}SceKernelAllocMemBlockOpt;
SceUID sceKernelAllocMemBlock(const char*,int,SceSize,const SceKernelAllocMemBlockOpt*);
int sceKernelGetMemBlockBase(SceUID,void**);int sceKernelFreeMemBlock(SceUID);
