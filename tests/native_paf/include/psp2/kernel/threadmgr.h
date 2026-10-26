#ifndef HOST_NATIVE_PAF_THREADMGR_H
#define HOST_NATIVE_PAF_THREADMGR_H
#include <stddef.h>
typedef int SceUID;
typedef unsigned SceSize;
typedef int (*SceKernelThreadEntry)(SceSize, void *);
SceUID sceKernelCreateThread(const char *, SceKernelThreadEntry, int, unsigned, unsigned, int, const void *);
int sceKernelStartThread(SceUID, SceSize, void *);
int sceKernelDeleteThread(SceUID);
int sceKernelDelayThread(unsigned);
int sceKernelGetThreadId(void);
#endif
