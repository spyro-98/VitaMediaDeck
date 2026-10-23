#ifndef VMD_PERSONAL_THREAD_H
#define VMD_PERSONAL_THREAD_H
#include <stdint.h>
typedef int SceUID;typedef unsigned SceSize;
int sceKernelDelayThread(unsigned);
int sceKernelCreateThread(const char*,int(*)(SceSize,void*),int,unsigned,int,int,void*);
int sceKernelStartThread(SceUID,SceSize,void*);
int sceKernelWaitThreadEnd(SceUID,void*,void*);
int sceKernelDeleteThread(SceUID);
int sceKernelExitThread(int);
int sceKernelChangeThreadPriority(SceUID,int);
#endif
SceUID sceKernelGetThreadId(void);
