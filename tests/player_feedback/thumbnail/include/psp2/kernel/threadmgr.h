#ifndef VMD_THUMB_THREAD_STUB
#define VMD_THUMB_THREAD_STUB
#include <psp2/io/fcntl.h>
int sceKernelExitThread(int);
int sceKernelWaitThreadEnd(SceUID,void *,void *);
int sceKernelDeleteThread(SceUID);
int sceKernelGetThreadCurrentPriority(void);
int sceKernelChangeThreadPriority(SceUID,int);
SceUID sceKernelGetThreadId(void);
SceUID sceKernelCreateThread(const char *,int (*)(SceSize,void *),int,unsigned,int,int,void *);
int sceKernelStartThread(SceUID,SceSize,void *);
#endif
