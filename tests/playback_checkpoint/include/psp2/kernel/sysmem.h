#ifndef VMD_CHECKPOINT_SYSMEM_H
#define VMD_CHECKPOINT_SYSMEM_H
typedef struct {int size,size_user,size_cdram,size_phycont;} SceKernelFreeMemorySizeInfo;
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *);
#endif
