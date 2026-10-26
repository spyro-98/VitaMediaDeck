#ifndef VMD_TEST_THREADMGR_H
#define VMD_TEST_THREADMGR_H
#include <stdint.h>
#include <stddef.h>
typedef int SceUID;
typedef unsigned SceSize;
typedef int (*SceKernelThreadEntry)(SceSize,void *);
typedef int (*SceKernelCallbackFunction)(int,int,int,void *);
uint64_t sceKernelGetProcessTimeWide(void);
SceUID sceKernelGetThreadId(void);
int sceKernelChangeThreadPriority(SceUID,int);
SceUID sceKernelCreateThread(const char *,SceKernelThreadEntry,int,unsigned,unsigned,int,void *);
int sceKernelStartThread(SceUID,SceSize,void *);
int sceKernelWaitThreadEnd(SceUID,int *,void *);
int sceKernelDeleteThread(SceUID);
int sceKernelExitThread(int);
int sceKernelDelayThreadCB(unsigned);
int sceKernelCreateCallback(const char *,unsigned,SceKernelCallbackFunction,void *);
int sceKernelDeleteCallback(SceUID);
#endif
