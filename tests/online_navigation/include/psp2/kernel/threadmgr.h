#ifndef VMD_TEST_THREADMGR_H
#define VMD_TEST_THREADMGR_H
#include <stddef.h>
typedef int SceUID;
typedef size_t SceSize;
int sceKernelExitThread(int);
int sceKernelWaitThreadEnd(SceUID, void *, void *);
int sceKernelDeleteThread(SceUID);
SceUID sceKernelCreateThread(const char *, int (*)(SceSize, void *), int, size_t, int, int, void *);
int sceKernelStartThread(SceUID, SceSize, void *);
#endif
